#include "LVGL_Example.h"

/**********************
 *      TYPEDEFS
 **********************/
typedef enum {
    DISP_SMALL,
    DISP_MEDIUM,
    DISP_LARGE,
} disp_size_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void Onboard_create(lv_obj_t * parent);

static void ta_event_cb(lv_event_t * e);
void example1_increase_lvgl_tick(lv_timer_t * t);
/**********************
 *  STATIC VARIABLES
 **********************/
static disp_size_t disp_size;

static lv_obj_t * tv;
lv_style_t style_text_muted;
lv_style_t style_title;
static lv_style_t style_icon;
static lv_style_t style_bullet;



static const lv_font_t * font_large;
static const lv_font_t * font_normal;

static lv_timer_t * auto_step_timer;

static lv_timer_t * meter2_timer;

lv_obj_t * SD_Size;
lv_obj_t * FlashSize;
lv_obj_t * Board_angle;
lv_obj_t * RTC_Time;
lv_obj_t * Wireless_Scan;



void IRAM_ATTR auto_switch(lv_timer_t * t)
{
  uint16_t page = lv_tabview_get_tab_act(tv);

  if (page == 0) { 
    lv_tabview_set_act(tv, 1, LV_ANIM_ON); 
  } else if (page == 3) {
    lv_tabview_set_act(tv, 2, LV_ANIM_ON); 
  }
}
void Lvgl_Example1(void){

  disp_size = DISP_SMALL;                            

  font_large = LV_FONT_DEFAULT;                             
  font_normal = LV_FONT_DEFAULT;                         
  
  lv_coord_t tab_h;
  tab_h = 45;
  #if LV_FONT_MONTSERRAT_18
    font_large     = &lv_font_montserrat_18;
  #else
    LV_LOG_WARN("LV_FONT_MONTSERRAT_18 is not enabled for the widgets demo. Using LV_FONT_DEFAULT instead.");
  #endif
  #if LV_FONT_MONTSERRAT_12
    font_normal    = &lv_font_montserrat_12;
  #else
    LV_LOG_WARN("LV_FONT_MONTSERRAT_12 is not enabled for the widgets demo. Using LV_FONT_DEFAULT instead.");
  #endif
  
  lv_style_init(&style_text_muted);
  lv_style_set_text_opa(&style_text_muted, LV_OPA_90);

  lv_style_init(&style_title);
  lv_style_set_text_font(&style_title, font_large);

  lv_style_init(&style_icon);
  lv_style_set_text_color(&style_icon, lv_theme_get_color_primary(NULL));
  lv_style_set_text_font(&style_icon, font_large);

  lv_style_init(&style_bullet);
  lv_style_set_border_width(&style_bullet, 0);
  lv_style_set_radius(&style_bullet, LV_RADIUS_CIRCLE);

  tv = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, tab_h);

  lv_obj_set_style_text_font(lv_scr_act(), font_normal, 0);


  lv_obj_t * t1 = lv_tabview_add_tab(tv, "Onboard");
  
  
  Onboard_create(t1);
  
  
}

void Lvgl_Example1_close(void)
{
  /*Delete all animation*/
  lv_anim_del(NULL, NULL);

  lv_timer_del(meter2_timer);
  meter2_timer = NULL;

  lv_obj_clean(lv_scr_act());

  lv_style_reset(&style_text_muted);
  lv_style_reset(&style_title);
  lv_style_reset(&style_icon);
  lv_style_reset(&style_bullet);
}


/**********************
*   STATIC FUNCTIONS
**********************/

static void Onboard_create(lv_obj_t * parent)
{

  /*Create a panel*/
  lv_obj_t * panel1 = lv_obj_create(parent);
  lv_obj_set_height(panel1, LV_SIZE_CONTENT);

  lv_obj_t * panel1_title = lv_label_create(panel1);
  lv_label_set_text(panel1_title, "Onboard parameter");
  lv_obj_add_style(panel1_title, &style_title, 0);

  lv_obj_t * SD_label = lv_label_create(panel1);
  lv_label_set_text(SD_label, "SD Card");
  lv_obj_add_style(SD_label, &style_text_muted, 0);

  SD_Size = lv_textarea_create(panel1);
  lv_textarea_set_one_line(SD_Size, true);
  lv_textarea_set_placeholder_text(SD_Size, "SD Size");
  lv_obj_add_event_cb(SD_Size, ta_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_t * Flash_label = lv_label_create(panel1);
  lv_label_set_text(Flash_label, "Flash Size");
  lv_obj_add_style(Flash_label, &style_text_muted, 0);

  FlashSize = lv_textarea_create(panel1);
  lv_textarea_set_one_line(FlashSize, true);
  lv_textarea_set_placeholder_text(FlashSize, "Flash Size");
  lv_obj_add_event_cb(FlashSize, ta_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_t * Wireless_label = lv_label_create(panel1);
  lv_label_set_text(Wireless_label, "Wireless scan");
  lv_obj_add_style(Wireless_label, &style_text_muted, 0);

  Wireless_Scan = lv_textarea_create(panel1);
  lv_textarea_set_one_line(Wireless_Scan, true);
  lv_textarea_set_placeholder_text(Wireless_Scan, "Wireless number");
  lv_obj_add_event_cb(Wireless_Scan, ta_event_cb, LV_EVENT_ALL, NULL);

  // 器件布局
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(parent, grid_main_col_dsc, grid_main_row_dsc);


  /*Create the top panel*/
  static lv_coord_t grid_1_col_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_1_row_dsc[] = {LV_GRID_CONTENT, /*Avatar*/
                                        LV_GRID_CONTENT, /*Name*/
                                        LV_GRID_CONTENT, /*Description*/
                                        LV_GRID_CONTENT, /*Email*/
                                        LV_GRID_CONTENT, /*Phone number*/
                                        LV_GRID_CONTENT, /*Button1*/
                                        LV_GRID_CONTENT, /*Button2*/
                                        LV_GRID_TEMPLATE_LAST
                                        };

  lv_obj_set_grid_dsc_array(panel1, grid_1_col_dsc, grid_1_row_dsc);


  static lv_coord_t grid_2_col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_2_row_dsc[] = {
    LV_GRID_CONTENT,  /*Title*/
    5,                /*Separator*/
    LV_GRID_CONTENT,  /*Box title*/
    40,               /*Box*/
    LV_GRID_CONTENT,  /*Box title*/
    40,               /*Box*/
    LV_GRID_CONTENT,  /*Box title*/
    40,               /*Box*/
    LV_GRID_CONTENT,  /*Box title*/
    40,               /*Box*/
    LV_GRID_CONTENT,  /*Box title*/
    40,               /*Box*/
    LV_GRID_CONTENT,  /*Box title*/
    40,               /*Box*/
    LV_GRID_TEMPLATE_LAST               
  };

  // lv_obj_set_grid_dsc_array(panel2, grid_2_col_dsc, grid_2_row_dsc);
  // lv_obj_set_grid_dsc_array(panel3, grid_2_col_dsc, grid_2_row_dsc);

  lv_obj_set_grid_cell(panel1, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_START, 0, 1);
  lv_obj_set_grid_dsc_array(panel1, grid_2_col_dsc, grid_2_row_dsc);
  lv_obj_set_grid_cell(panel1_title, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(SD_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 2, 1);
  lv_obj_set_grid_cell(SD_Size, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 3, 1);
  lv_obj_set_grid_cell(Flash_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 4, 1);
  lv_obj_set_grid_cell(FlashSize, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 5, 1);
  lv_obj_set_grid_cell(Wireless_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 6, 1);
  lv_obj_set_grid_cell(Wireless_Scan, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 7, 1);

  // 器件布局 END
  
  auto_step_timer = lv_timer_create(example1_increase_lvgl_tick, 100, NULL);
}

void example1_increase_lvgl_tick(lv_timer_t * t)
{
  char buf[100]={0}; 
  
  snprintf(buf, sizeof(buf), "%ld MB\r\n", SDCard_Size);
  lv_textarea_set_placeholder_text(SD_Size, buf);
  snprintf(buf, sizeof(buf), "%ld MB\r\n", Flash_Size);
  lv_textarea_set_placeholder_text(FlashSize, buf);
  if(Scan_finish)
    snprintf(buf, sizeof(buf), "W: %d  B: %d    OK.\r\n",WIFI_NUM,BLE_NUM);
    // snprintf(buf, sizeof(buf), "WIFI: %d     ..OK.\r\n",WIFI_NUM);
  else
    snprintf(buf, sizeof(buf), "W: %d  B: %d\r\n",WIFI_NUM,BLE_NUM);
    // snprintf(buf, sizeof(buf), "WIFI: %d  \r\n",WIFI_NUM);
  lv_textarea_set_placeholder_text(Wireless_Scan, buf);
}

static void ta_event_cb(lv_event_t * e)
{
}





