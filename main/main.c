#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "led_strip.h"
#include "cJSON.h"
#include <esp_http_server.h>

#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN

QueueHandle_t data_ws_in_queue = NULL;

static const char *TAG = "POKEBALL";

const char* base_path = "/data";

static httpd_handle_t server = NULL;

bool pokeball_connected;

//Struct for RGB LED colors
typedef struct{
	uint8_t R_color;
	uint8_t G_color;
	uint8_t B_color;
}t_rgb_color;

#define RGB_LEDS_DI_BOTTOM 44
led_strip_handle_t led_strip_bottom;

/****************************************************************************************************
 * FUNCT:   configure_led
 * BRIEF:   Addressable RGB Leds configuration function
 * RETURN:  void
 * ARG:     void
 ****************************************************************************************************/
static void configure_led(void){
    //ESP_LOGI(TAG, "Configure RGB LEDs");
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LEDS_DI_BOTTOM,
        .max_leds = 24, // at least one LED on board
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_bottom));
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip_bottom);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
        pokeball_connected = true;
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
        pokeball_connected = false;
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

/* Function to initialize SPIFFS */
esp_err_t spiffs_mount_storage(const char* base_path)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = base_path,
        .partition_label = NULL,
        .max_files = 5,   // This sets the maximum number of files that can be open at the same time
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    return ESP_OK;
}

/* handler for websocket URI request */
static esp_err_t handle_ws_req(httpd_req_t *req)
{
    t_rgb_color pixel_to_queue;


    if (req->method == HTTP_GET)
    {
        //ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len)
    {

        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
    	ESP_LOGI(TAG,"ws received packet lenght: %d",ws_pkt.len);
    	ESP_LOGI(TAG,"data: %s",ws_pkt.payload);
        xQueueSend(data_ws_in_queue,ws_pkt.payload,NULL);

        free(buf);
    }
    return ESP_OK;
}

/* An HTTP GET handler */
static esp_err_t index_html_get_handler(httpd_req_t *req)
{

    /* Get handle to embedded file upload script */
    extern const unsigned char index_html_start[] asm("_binary_index_html_start");
    extern const unsigned char index_html_end[]   asm("_binary_index_html_end");
    const size_t index_html_size = (index_html_end - index_html_start);
    ESP_LOGI(TAG, "File size html: %d",index_html_size);

    /* Add file upload form and script which on execution sends a POST request to /upload */
    //httpd_resp_send(req, (const char *)index_html_start, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send(req, (const char *)index_html_start, index_html_size);
    //httpd_resp_send_chunk(req, (const char *)index_html_start, index_html_size);

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}

/* An HTTP GET handler */
static esp_err_t style_css_get_handler(httpd_req_t *req)
{
    /* Get handle to embedded file upload script */
    extern const unsigned char style_css_start[] asm("_binary_styles_css_start");
    extern const unsigned char style_css_end[]   asm("_binary_styles_css_end");
    const size_t style_css_size = (style_css_end - style_css_start);
    ESP_LOGI(TAG, "File size css: %d",style_css_size);

    /* Add file upload form and script which on execution sends a POST request to /upload */
    //httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_set_type(req, "text/css");

    httpd_resp_send(req, (const char *)style_css_start, style_css_size);
    //httpd_resp_send_chunk(req, (const char *)style_css_start, style_css_size);

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}


/* An HTTP GET handler */
static esp_err_t script_js_get_handler(httpd_req_t *req)
{

    /* Get handle to embedded file upload script */
    extern const unsigned char script_js_start[] asm("_binary_script_js_start");
    extern const unsigned char script_js_end[]   asm("_binary_script_js_end");
    const size_t script_js_size = (script_js_end - script_js_start);
    ESP_LOGI(TAG, "File size js: %d",script_js_size);

    /* Add file upload form and script which on execution sends a POST request to /upload */
    /* Set error code in HTTP response */
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    httpd_resp_send(req, (const char *)script_js_start, script_js_size);
    //httpd_resp_send_chunk(req, (const char *)script_js_start, script_js_size);

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = handle_ws_req,
        .user_ctx = NULL,
        .is_websocket = true
    };
    httpd_uri_t index_html = {
        .uri       = "/index.html",
        .method    = HTTP_GET,
        .handler   = index_html_get_handler,
        /* Let's pass response string in user
         * context to demonstrate it's usage */
        .user_ctx  = "Index_html"
    };

    httpd_uri_t style_css = {
        .uri       = "/styles.css",
        .method    = HTTP_GET,
        .handler   = style_css_get_handler,
        .user_ctx  = "style_css"
    };

    httpd_uri_t script_js = {
        .uri       = "/script.js",
        .method    = HTTP_GET,
        .handler   = script_js_get_handler,
        .user_ctx  = "script_js"
    };

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &index_html);
        httpd_register_uri_handler(server, &style_css);
        httpd_register_uri_handler(server, &script_js);
        httpd_register_uri_handler(server, &ws);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");

    return NULL;
}

// Function to rotate 3 pixels around the LED ring 10 times
void rotate_pixels(led_strip_handle_t strip, int num_leds,t_rgb_color color) {
    const int rotations = 2;
    const int delay_ms = 20;  // Change this to control the rotation speed

    for (int rotation = 0; rotation < rotations * num_leds; rotation++) {
        // Clear all LEDs
        for (int i = 0; i < num_leds; i++) {
            led_strip_set_pixel(strip, i, 0, 0, 0);
        }

        // Set 3 consecutive LEDs to red
        for (int i = 0; i < 8; i++) {
            int led_idx = (rotation + i) % num_leds;
            led_strip_set_pixel(strip, led_idx, color.R_color, color.G_color, color.B_color);
        }

        led_strip_refresh(strip);  // Send data to the strip
        vTaskDelay(pdMS_TO_TICKS(delay_ms));  // Delay between rotations
    }
}

// Function to create a pulsing light effect on the LED ring
void pulse_pixels(led_strip_handle_t strip, int num_leds, t_rgb_color color) {
    const int pulse_steps = 50;    // Number of steps to go from minimum to maximum brightness
    const int delay_ms = 20;       // Delay between each step for a smooth pulsing effect

    // Increase brightness
    for (int step = 0; step <= pulse_steps; step++) {
        float brightness = (float)step / pulse_steps;
        int red = color.R_color * brightness;
        int green = color.G_color * brightness;
        int blue = color.B_color * brightness;

        for (int i = 0; i < num_leds; i++) {
            led_strip_set_pixel(strip, i, red, green, blue);
        }

        led_strip_refresh(strip);  // Send data to the strip
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    // Decrease brightness
    for (int step = pulse_steps; step >= 0; step--) {
        float brightness = (float)step / pulse_steps;
        int red = color.R_color * brightness;
        int green = color.G_color * brightness;
        int blue = color.B_color * brightness;

        for (int i = 0; i < num_leds; i++) {
            led_strip_set_pixel(strip, i, red, green, blue);
        }

        led_strip_refresh(strip);  // Send data to the strip
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}


static void led_s_task(void *arg){
	t_rgb_color led_color;
	t_rgb_color pixel_from_queue;
	led_color.B_color = 0;
	led_color.G_color = 0;
	led_color.R_color = 75;
	char s_json_from_queue[1024];
	cJSON *dataIn;
	cJSON *pixel_array;


	while(1){
		if(pokeball_connected == false){
			led_color.B_color = 0;
			led_color.G_color = 0;
			led_color.R_color = 75;
			rotate_pixels(led_strip_bottom, 24, led_color);
		}else{
			if(xQueueReceive(data_ws_in_queue, s_json_from_queue, 0) != 0){
	            dataIn = cJSON_Parse((char *)s_json_from_queue);
	            if(dataIn != NULL){
		            pixel_array = cJSON_GetObjectItem(dataIn,"pixel");
		            if(pixel_array!=NULL){
		            	int pixels_array_size = cJSON_GetArraySize(pixel_array);
		            	for(int pix_indx = 0; pix_indx < pixels_array_size; pix_indx ++){
		            		cJSON *pixel = cJSON_GetArrayItem(pixel_array,pix_indx);
		                	if(pixel != NULL){
		                        int pix_id = cJSON_GetObjectItem(pixel,"id")->valueint;
		                        pixel_from_queue.R_color = cJSON_GetObjectItem(pixel,"R")->valueint;
		                        pixel_from_queue.G_color = cJSON_GetObjectItem(pixel,"G")->valueint;
		                        pixel_from_queue.B_color = cJSON_GetObjectItem(pixel,"B")->valueint;
		                        if(pix_id == -1){		//-1 for all leds turn same color
		                        	ESP_LOGI(TAG,"All LEDs same color");
		                            for (int i = 0; i < 24; i++) {
		                                led_strip_set_pixel(led_strip_bottom, i,  pixel_from_queue.R_color,  pixel_from_queue.G_color,  pixel_from_queue.B_color);
		                            }
		                        }else{
		                        	led_strip_set_pixel(led_strip_bottom, pix_id, pixel_from_queue.R_color, pixel_from_queue.G_color, pixel_from_queue.B_color);
		                        }
		                    }
		            	}
		            }
		            led_strip_refresh(led_strip_bottom);  // Send data to the strip
		            vTaskDelay(pdMS_TO_TICKS(50));  // Delay
		            cJSON_Delete(dataIn);
	            }else{
	            	ESP_LOGI(TAG, "json dataIn NULL pointer" );
					if(led_color.B_color == 75){
						led_color.B_color = 0;
						led_color.G_color = 0;
					}else{
						led_color.B_color = 75;
						led_color.G_color = 75;
					}
					led_color.R_color = 75;
					pulse_pixels(led_strip_bottom, 24, led_color);
	            }
			}else{
				vTaskDelay(pdMS_TO_TICKS(50));  // Delay
			}
		}
	}
}

void app_main(void)
{
	//Configure LEDs stripes
	configure_led();
	pokeball_connected = false;

    data_ws_in_queue = xQueueCreate(5, 1024*sizeof(char));

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize file storage */
    ESP_ERROR_CHECK(spiffs_mount_storage(base_path));

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

    /* Start the server for the first time */
    server = start_webserver();

    xTaskCreatePinnedToCore(led_s_task, "leds_s", 4*1024, NULL, 5, NULL,0);
}
