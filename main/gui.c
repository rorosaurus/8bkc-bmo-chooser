#include <string.h>
#include <stdint.h>
#include <malloc.h>
#include <stdio.h>
#include "gui.h"
#include "ugui.h"
#include "8bkc-hal.h"
#include "8bkc-ugui.h"
#include "appfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "nvs.h"
#include "nvs_flash.h"

static const uint8_t gfx[]={
#include "graphics.inc"
};

#define GFX_W 41
#define GFX_H 33
#define GFX_IH 11

#define GFX_O_EMPTY 0
#define GFX_O_FULL 1
#define GFX_O_CHG 2
#define GFX_O_CHGNEARFULL 3


void drawIcon(int px, int py, int o) {
	const uint8_t *p=&gfx[o*GFX_IH*GFX_W*4];
	for (int y=0; y<GFX_IH; y++) {
		for (int x=0; x<GFX_W; x++) {
			UG_DrawPixel(x+px, y+py, kchal_ugui_rgb(p[0],p[1],p[2]));
			p+=4;
		}
	}
}

static void renderGfx(uint16_t *ovl, int dx, int dy, int sx, int sy, int sw, int sh) {
	//uint32_t *gfx=(uint32_t*)graphics;
	int x, y, i;
	if (dx<0) {
		sx-=dx;
		sw+=dx;
		dx=0;
	}
	if ((dx+sw)>80) {
		sw-=((dx+sw)-80);
		dx=80-sw;
	}
	if (dy<0) {
		sy-=dy;
		sh+=dy;
		dy=0;
	}
	if ((dy+sh)>64) {
		sh-=((dy+sh)-64);
		dy=64-sh;
	}

	for (y=0; y<sh; y++) {
		for (x=0; x<sw; x++) {
			i=gfx[(sy+y)*80+(sx+x)];
			if (i&0x80000000) ovl[(dy+y)*80+(dx+x)]=kchal_fbval_rgb((i>>0)&0xff, (i>>8)&0xff, (i>>16)&0xff);
		}
	}
}


void guiCharging(int almostFull) {
	kcugui_cls();
	if (!almostFull) {
		drawIcon(20, 26, GFX_O_CHG);
	} else {
		drawIcon(20, 26, GFX_O_CHGNEARFULL);
	}
	kcugui_flush();
}

void guiFull() {
	kcugui_cls();
	drawIcon(20, 26, GFX_O_FULL);
	kcugui_flush();
}

void guiBatEmpty() {
	kcugui_cls();
	drawIcon(20, 26, GFX_O_EMPTY);
	kcugui_flush();
}

void guiInit() {
	kcugui_init();
	uint8_t wifi_en=1;
	nvs_handle nvsHandle=NULL;
	esp_err_t r=nvs_open("8bkc", NVS_READONLY, &nvsHandle);
	if (r==ESP_OK) {
		nvs_get_u8(nvsHandle, "wifi", &wifi_en);
	}
	nvs_close(nvsHandle);

	UG_FontSelect(&FONT_6X8);
	if (wifi_en) {
		UG_SetForecolor(C_WHITE);
		UG_PutString(0, 0, "WIFI AP");
		UG_SetForecolor(C_YELLOW);
		UG_PutString(0, 8, " pkspr");
		UG_SetForecolor(C_WHITE);
		UG_PutString(0, 16, "GO TO:");
		UG_SetForecolor(C_YELLOW);
		UG_PutString(0, 24, "HTTP://192.168.4.1/");
	} else {
		UG_SetForecolor(C_WHITE);
		UG_PutString(0, 0, "   NOTE:");
		UG_SetForecolor(C_YELLOW);
		UG_PutString(0, 8,  "WiFi is off");
		UG_PutString(0, 16, "and can be ");
		UG_PutString(0, 24, "enabled in ");
		UG_PutString(0, 32, "the options");
		UG_PutString(0, 40, "menu.      ");
	}
	UG_SetForecolor(C_RED);
	UG_PutString(30, 56, "MENU");
	UG_SetBackcolor(C_BLACK);

	kcugui_flush();
}

#define ST_START 0
#define ST_SELECT 1
#define ST_REBOOT 2


#include "8bkcgui-widgets.h"

static void debug_screen() {
	char buf[32];
	int k, v;
	int selReleased=0;
	nvs_handle nvsHandle=NULL;
	uint32_t battFullAdcVal=0;
	esp_err_t r=nvs_open("8bkc", NVS_READONLY, &nvsHandle);
	if (r==ESP_OK) {
		nvs_get_u32(nvsHandle, "batadc", &battFullAdcVal);
	}
	nvs_close(nvsHandle);
	while(1) {
		kcugui_cls();
		UG_FontSelect(&FONT_6X8);
		UG_SetForecolor(C_YELLOW);
		UG_PutString(0, 0, "DEBUG");
		UG_SetForecolor(C_WHITE);
		k=kchal_get_keys();
		sprintf(buf, "KEYS: %X", k);
		UG_PutString(0, 8, buf);
		v=kchal_get_chg_status();
		sprintf(buf, "CHG: %X", v);
		UG_PutString(0, 16, buf);
		v=kchal_get_bat_mv();
		sprintf(buf, "VBATMV:%d", v);
		UG_PutString(0, 24, buf);
		sprintf(buf, "ADCCAL:%d", battFullAdcVal);
		UG_PutString(0, 32, buf);
		UG_SetForecolor(C_YELLOW);
		UG_PutString(0, 40, "Gitrev/date");
		UG_SetForecolor(C_WHITE);
		UG_PutString(0, 48, GITREV);
		UG_PutString(0, 56, COMPILEDATE);
		kcugui_flush();
		vTaskDelay(100/portTICK_RATE_MS);
		if (k&KC_BTN_SELECT) {
			if (selReleased) break;
		} else {
			selReleased=1;
		}
	}
}


#define OPT_KEYLOCK 0
#define OPT_WIFI 1
#define OPT_VOL 2
#define OPT_BRIGHT 3
#define OPT_CHANNEL 4

typedef struct {
	int opt_id;
	char *opt_name;
	int *opt_val;
} opt_data_t;

void option_set_text(opt_data_t *t) {
	if (t->opt_id==OPT_KEYLOCK) {
		sprintf(t->opt_name, "Keylock %s", *t->opt_val?"ON":"OFF");
	} else if (t->opt_id==OPT_WIFI) {
		sprintf(t->opt_name, "WiFi    %s", *t->opt_val?"ON":"OFF");
	} else if (t->opt_id==OPT_CHANNEL) {
		sprintf(t->opt_name, "Channel %d", *t->opt_val);
	} else if (t->opt_id==OPT_VOL) {
		sprintf(t->opt_name, "Volume %d", kchal_get_volume());
	} else if (t->opt_id==OPT_BRIGHT) {
		sprintf(t->opt_name, "Bright %d", kchal_get_brightness());
	}
}

int option_menu_cb(int button, char **desc, kcugui_menuitem_t **menu, int item_selected, void *userptr) {
	opt_data_t *od=(*menu)[item_selected].user;
	if (button==KC_BTN_B) return KCUGUI_CB_CANCEL;
	if (button!=KC_BTN_LEFT && button!=KC_BTN_RIGHT) return 0;
	if (od->opt_id==OPT_KEYLOCK || od->opt_id==OPT_WIFI) {
		*od->opt_val=!(*od->opt_val);
	} else if (od->opt_id==OPT_VOL) {
		int n=kchal_get_volume();
		if (button==KC_BTN_LEFT) n-=10; else n+=10;
		if (n<0) n=0;
		if (n>255) n=255;
		kchal_set_volume(n);
	} else if (od->opt_id==OPT_BRIGHT) {
		int n=kchal_get_brightness();
		if (button==KC_BTN_LEFT) n-=10; else n+=10;
		if (n<0) n=0;
		if (n>255) n=255;
		kchal_set_brightness(n);
	} else if (od->opt_id==OPT_CHANNEL) {
		int n=*od->opt_val;
		if (button==KC_BTN_LEFT) n-=1; else n+=1;
		if (n<1) n=1;
		if (n>13) n=13;
		*od->opt_val=n;
	}
	option_set_text(od);
	return KCUGUI_CB_REFRESH;
}


static void show_options() {
	int keylock=false, wifi_en=true, channel=5;
	char text[5][32];
	opt_data_t odata[]={
		{OPT_KEYLOCK, text[0], &keylock},
		{OPT_WIFI, text[1], &wifi_en},
		{OPT_VOL, text[2], NULL},
		{OPT_BRIGHT, text[3], NULL},
		{OPT_CHANNEL, text[4], &channel},
	};
	kcugui_menuitem_t menu[]={
		{text[0],0,&odata[0]},
		{text[1],0,&odata[1]},
		{text[4],0,&odata[4]},
		{text[2],0,&odata[2]},
		{text[3],0,&odata[3]},
		{"Debug info", 0, NULL},
		{"Exit",0,NULL},
		{"",KCUGUI_MENUITEM_LAST,0,NULL}
	};
	nvs_handle nvsHandle=NULL;
	esp_err_t r=nvs_open("8bkc", NVS_READWRITE, &nvsHandle);
	nvs_get_u8(nvsHandle, "kl", &keylock);
	nvs_get_u8(nvsHandle, "wifi", &wifi_en);
	nvs_get_u8(nvsHandle, "channel", &channel);
	keylock&=255; wifi_en&=255; //uint8 -> int
	int wifi_old=wifi_en;
	int channel_old=channel;
	if (channel<1 || channel>13) channel=5;
	for(int i=0; i<5; i++) option_set_text(&odata[i]);
	int ch=-2;
	while(ch!=6 && ch!=-1) {
		ch=kcugui_menu(menu, "OPTIONS", option_menu_cb, NULL);
		if (ch==0) {
			keylock=!keylock;
			option_set_text(&odata[0]);
		}
		if (ch==1) {
			wifi_en=!wifi_en;
			option_set_text(&odata[1]);
		}
		if (ch==5) {
			debug_screen();
		}
	}
	nvs_set_u8(nvsHandle, "kl", keylock);
	nvs_set_u8(nvsHandle, "wifi", wifi_en);
	nvs_set_u8(nvsHandle, "channel", channel);
	if (wifi_old!=wifi_en || channel!=channel_old) {
		//Reboot I guess
		kchal_boot_into_new_app();
	}
}

static int fccallback(int button, char **glob, char **desc, void *usrptr) {
	if (button & KC_BTN_POWER) kchal_power_down();
	if (button & KC_BTN_START) show_options();
	if (button & KC_BTN_SELECT) debug_screen();
	return 0;
}

int app_select_filter_fn(const char *name, void *filterarg) {
	if (strcmp(name, "chooser.app")==0) return 0; //filter out this app, kinda stupid to start that from the menu.
	return kcugui_filechooser_filter_glob(name, filterarg);
}

void guiMenu() {
	//Wait till all buttons are released, and then until one button is pressed to go into the menu.
	while (kchal_get_keys()) vTaskDelay(100/portTICK_RATE_MS);
	while (!kchal_get_keys()) {
		uint16_t *fb = kcugui_get_fb();
		// draw empty battery cell
		renderGfx(fb, KC_SCREEN_W-16, 0, 0, 44, 16, 7);
		
		// fill in the battery with appropriate color
		int batPct = kchal_get_bat_pct();
		if (batPct < 20) renderGfx(fb, KC_SCREEN_W-15, 1, 17, 45, (batPct*12)/100, 5);
		else if (batPct < 50) renderGfx(fb, KC_SCREEN_W-15, 1, 0, 52, (batPct*12)/100, 5);
		else renderGfx(fb, KC_SCREEN_W-15, 1, 17, 52, (batPct*12)/100, 5);
		
		// add lightning bolt icon if applicable
		if (kchal_get_chg_status() > 0) {
			renderGfx(fb, KC_SCREEN_W-11, 0, 33, 44, 6, 8);
		}
		
		vTaskDelay(100/portTICK_RATE_MS);
	}
	int fd=kcugui_filechooser_filter(app_select_filter_fn, "*.app,*.bin", "CHOOSE APP", fccallback, NULL, KCUGUI_FILE_FLAGS_NOEXT);
	while(kchal_get_keys()); //wait till btn released
	kchal_set_new_app(fd);
	kchal_boot_into_new_app();
}


