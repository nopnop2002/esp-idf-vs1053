/* VT1053 driver Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/ringbuf.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "vs1053.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_ESP_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID	   CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS	   CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT	BIT0
#define WIFI_FAIL_BIT		BIT1

#define HTTP_RESUME_BIT		BIT0
#define HTTP_CLOSE_BIT		BIT2

static const char *TAG = "MAIN";

static int s_retry_num = 0;

RingbufHandle_t xRingbuffer1;
RingbufHandle_t xRingbuffer2;
EventGroupHandle_t xEventGroup;

static void event_handler(void* arg, esp_event_base_t event_base,
								int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

esp_err_t wifi_init_sta(void)
{
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = EXAMPLE_ESP_WIFI_SSID,
			.password = EXAMPLE_ESP_WIFI_PASS
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
			WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
			pdFALSE,
			pdFALSE,
			portMAX_DELAY);

	esp_err_t rcode = ESP_FAIL;
	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
				 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
		rcode = ESP_OK;
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
				 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
	}

	ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
	ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
	vEventGroupDelete(s_wifi_event_group);
	return rcode;
}



#define MAX_HTTP_RECV_BUFFER 512
#define MIN_HTTP_RECV_BUFFER 32

static void console_task(void *pvParameters)
{
	ESP_LOGI(pcTaskGetTaskName(0), "Start");
	char *buffer = malloc(MAX_HTTP_RECV_BUFFER + 1);
	if (buffer == NULL) {
		ESP_LOGE(pcTaskGetTaskName(0), "Cannot malloc http receive buffer");
		while(1) { vTaskDelay(1); }
	}

	size_t item_size;
	while (1) {
		char *item = (char *)xRingbufferReceive(xRingbuffer1, &item_size, pdMS_TO_TICKS(1000));
		if (item != NULL) {
			ESP_LOGI(pcTaskGetTaskName(0), "xRingbufferReceive item_size=%d", item_size);
			for (int i = 0; i < item_size; i++) {
				//printf("%c", item[i]);
				buffer[i] = item[i];
				buffer[i+1] = 0;
			}
			ESP_LOGI(pcTaskGetTaskName(0),"\n%s",buffer);
			//Return Item
			vRingbufferReturnItem(xRingbuffer1, (void *)item);
		}
	}

	// never reach here
	free(buffer);
	ESP_LOGI(pcTaskGetTaskName(0), "Finish");
	vTaskDelete(NULL);
}

#if 0
#define CONFIG_GPIO_CS 5
#define CONFIG_GPIO_DCS 16
#define CONFIG_GPIO_DREQ 4
#define CONFIG_GPIO_RESET -1
#endif

static void vs1053_task(void *pvParameters)
{
	ESP_LOGI(pcTaskGetTaskName(0), "Start");
	VS1053_t dev;
	spi_master_init(&dev,CONFIG_GPIO_CS, CONFIG_GPIO_DCS, CONFIG_GPIO_DREQ, CONFIG_GPIO_RESET);
	ESP_LOGI(pcTaskGetTaskName(0), "spi_master_init done");
	switchToMp3Mode(&dev);
	setVolume(&dev, 100);

	char *buffer = malloc(MAX_HTTP_RECV_BUFFER + 1);
	if (buffer == NULL) {
		ESP_LOGE(pcTaskGetTaskName(0), "Cannot malloc http receive buffer");
		while(1) { vTaskDelay(1); }
	}

	// start http client
	xEventGroupSetBits( xEventGroup, HTTP_RESUME_BIT );
	ESP_LOGI(pcTaskGetTaskName(0), "xEventGroupSetBits");

	size_t item_size;
	while (1) {
		char *item = (char *)xRingbufferReceive(xRingbuffer2, &item_size, pdMS_TO_TICKS(1000));
		if (item != NULL) {
			ESP_LOGD(pcTaskGetTaskName(0), "xRingbufferReceive item_size=%d", item_size);
			for (int i = 0; i < item_size; i++) {
				buffer[i] = item[i];
				buffer[i+1] = 0;
			}
			playChunk(&dev, (uint8_t *)buffer, item_size);
			//Return Item
			vRingbufferReturnItem(xRingbuffer2, (void *)item);
		} else {
			//Failed to receive item
			ESP_LOGW(pcTaskGetTaskName(0), "No receive item");
		}
		//vTaskDelay(10);
	}

	// never reach here
	free(buffer);
	ESP_LOGI(pcTaskGetTaskName(0), "Finish");
	vTaskDelete(NULL);
}

int searchString(int * first, char * buf, char * target) {
		//static bool first = true;
		static char pdata[MAX_HTTP_RECV_BUFFER*2+1];
		static int prevl;
		char* t1;
		if (*first == 0) {
				strcpy(pdata, buf);
				prevl=strlen(buf);
				*first=1;
				return 0;
		} else {
				strcat(pdata,buf);
				t1 = strstr(pdata, target); // Found target
				if (t1 == NULL) {
						strcpy(pdata,buf);
						prevl=strlen(buf);
						return 0;
				} else {
						ESP_LOGD(TAG, "pdata=%p",pdata);
						ESP_LOGD(TAG, "t1	=%p",t1);
						int ret = t1 - pdata;
						return (ret - prevl + strlen(target));
				}
		}
}

#define SERVER_HOST	   CONFIG_SERVER_HOST
#define SERVER_PORT	   CONFIG_SERVER_PORT
#define SERVER_PATH	   CONFIG_SERVER_PATH

/*
.url = "http://ice2.somafm.com:80/seventies-128-mp3",

#define SERVER_HOST "ice2.somafm.com"
#define SERVER_PORT 80
#define SERVER_PATH "/seventies-128-mp3"
*/

static void client_task(void *pvParameters)
{
	ESP_LOGI(pcTaskGetTaskName(0), "Start");
	xEventGroupWaitBits( xEventGroup,
			HTTP_RESUME_BIT,/* The bits within the event group to wait for. */
			pdTRUE,			/* HTTP_RESUME_BIT should be cleared before returning. */
			pdFALSE,		/* Don't wait for both bits, either bit will do. */
			portMAX_DELAY);	/* Wait forever. */
	ESP_LOGI(pcTaskGetTaskName(0), "HTTP_RESUME_BIT");

	// set up address to connect to
	ESP_LOGI(pcTaskGetTaskName(0), "SERVER_HOST=%s", SERVER_HOST);
	ESP_LOGI(pcTaskGetTaskName(0), "SERVER_PORT=%d", SERVER_PORT);
	ESP_LOGI(pcTaskGetTaskName(0), "SERVER_PATH=%s", SERVER_PATH);
	struct sockaddr_in server;
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_port = htons(SERVER_PORT);
	server.sin_addr.s_addr = inet_addr(SERVER_HOST);
	if (server.sin_addr.s_addr == 0xffffffff) {
		struct hostent *host;
		host = gethostbyname(SERVER_HOST);
		if (host == NULL) {
			ESP_LOGE(TAG, "DNS lookup failed. Check %s:%d", SERVER_HOST, SERVER_PORT);
			while(1) vTaskDelay(10);
		} else {
			ESP_LOGI(TAG, "DNS lookup success");
		}
		server.sin_addr.s_addr = *(unsigned int *)host->h_addr_list[0];
	}


	// create the socket
	int fd;
	int ret;
	fd = socket(AF_INET, SOCK_STREAM, 0);
	LWIP_ASSERT("fd >= 0", fd >= 0);

	// connect to server
	ret = connect(fd, (struct sockaddr*)&server, sizeof(server));
	LWIP_ASSERT("ret == 0", ret == 0);
	ESP_LOGI(pcTaskGetTaskName(0), "Connect server");

	// allocate buffer
	char *buffer = malloc(MAX_HTTP_RECV_BUFFER + 1);
	if (buffer == NULL) {
		ESP_LOGE(pcTaskGetTaskName(0), "Cannot malloc http receive buffer");
		while(1) { vTaskDelay(1); }
	}

	// send request
	sprintf(buffer, "GET %s HTTP/1.1\r\n", SERVER_PATH);
	ret = send(fd, buffer, strlen(buffer), 0);
	LWIP_ASSERT("ret == strlen(buffer)", ret == strlen(buffer));

	sprintf(buffer, "HOST: %s\r\n", SERVER_HOST);
	ret = send(fd, buffer, strlen(buffer), 0);
	LWIP_ASSERT("ret == strlen(buffer)", ret == strlen(buffer));

	sprintf(buffer, "User-Agent: ESP32/1.00\r\n");
	ret = send(fd, buffer, strlen(buffer), 0);
	LWIP_ASSERT("ret == strlen(buffer)", ret == strlen(buffer));

	// ToDo:receive icy-metadata
	// https://stackoverflow.com/questions/44050266/get-info-from-streaming-radio
	sprintf(buffer, "Icy-MetaData: 0\r\n");
	ret = send(fd, buffer, strlen(buffer), 0);
	LWIP_ASSERT("ret == strlen(buffer)", ret == strlen(buffer));

	sprintf(buffer, "\r\n");
	//ret = lwip_write(fd, buffer, strlen(buffer));
	ret = send(fd, buffer, strlen(buffer), 0);
	LWIP_ASSERT("ret == strlen(buffer)", ret == strlen(buffer));

	// set timeout
	struct timeval timeout;
	timeout.tv_usec = 0;
	timeout.tv_sec = 3;
	setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));

	// main loop
	int first = 0;
	int status = 0;
	int buffer_size = MAX_HTTP_RECV_BUFFER;
	size_t	data_size;

	while(1) {
		// read data
		int read_len = read(fd, buffer, buffer_size);
		//int read_len = lwip_read(fd, buffer, buffer_size);
		//int read_len = lwip_read(fd, buffer, MAX_HTTP_RECV_BUFFER);
		if (read_len < 0) {
			// I don't know why it is disconnected from the server.
			// Maybe there is some bug.
			// But i cannot find.
			ESP_LOGW(pcTaskGetTaskName(0), "read_len = %d", read_len);
			break;
		}
		if (read_len == 0) continue;

		buffer[read_len] = 0;
		ESP_LOGD(pcTaskGetTaskName(0), "read_len = %d", read_len);
	
		// skip HTTP header
		// SHOUTcast server does not return header length.
		// Therefore, it is necessary to find the end of the header.
#if 0
HTTP/1.1 200 OK
Content-Type: audio/mpeg
Date: Thu, 12 Mar 2020 13:33:42 GMT
icy-br:128
icy-genre:Mellow 70s
icy-name:Left Coast 70s: Mellow album rock from the Seventies. Yacht friendly. [SomaFM]
icy-notice1:<BR>This stream requires <a href="http://www.winamp.com/">Winamp</a><BR>
icy-notice2:SHOUTcast Distributed Network Audio Server/Linux v1.9.5<BR>
icy-pub:0
icy-url:http://somafm.com
Server: Icecast 2.4.0-kh10
Cache-Control: no-cache, no-store
Access-Control-Allow-Origin: *
Access-Control-Allow-Headers: Origin, Accept, X-Requested-With, Content-Type
Access-Control-Allow-Methods: GET, OPTIONS, HEAD
Connection: Close
Expires: Mon, 26 Jul 1997 05:00:00 GMT
#endif

		
		if (status == 0) {
			int post = searchString(&first, buffer, "\r\n\r\n");
			ESP_LOGI(pcTaskGetTaskName(0), "post=%d", post);
			if (post == 0) { // all data is header
				ESP_LOGI(pcTaskGetTaskName(0), "xRingbuffer1 Send=%d", read_len);
				xRingbufferSend(xRingbuffer1, buffer, read_len, 0);
				data_size = 0;
			} else { // detecte end of header
				status++;
				ESP_LOGI(pcTaskGetTaskName(0), "xRingbuffer1 Send=%d", post);
				xRingbufferSend(xRingbuffer1, buffer, post, 0);
				data_size = read_len - post;
			}
		} else {
			data_size = read_len;
		}
#if 0
ToDo: 
Retrieve metadata embedded in data.
To do so, it is necessary to parse the HTTP header and extract the icy-metaint.
actual text metadata format:
StreamTitle='Buscemi - First Flight To London';StreamUrl='http://SomaFM.com/secretagent/';
#endif

		// Adjust the receive buffer size according to the free size of xRingbuffer.
		if (data_size) xRingbufferSend(xRingbuffer2, buffer, data_size, 0);
		size_t free_size = xRingbufferGetCurFreeSize(xRingbuffer2);
		ESP_LOGD(pcTaskGetTaskName(0), "free_size=%d buffer_size=%d", free_size, buffer_size);
		buffer_size = MIN_HTTP_RECV_BUFFER;
		if (free_size > MAX_HTTP_RECV_BUFFER*2) buffer_size = MAX_HTTP_RECV_BUFFER;
		// Required for other tasks to work.
		vTaskDelay(1);
	}

	free(buffer);
	// close socket
	ret = close(fd);
	LWIP_ASSERT("ret == 0", ret == 0);
	xEventGroupSetBits( xEventGroup, HTTP_CLOSE_BIT );
	ESP_LOGI(pcTaskGetTaskName(0), "Finish");
	vTaskDelete(NULL);
}

void app_main(void)
{
	// Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	  ESP_ERROR_CHECK(nvs_flash_erase());
	  ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	// Connect wifi
	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	ret = wifi_init_sta();
	if (ret != ESP_OK) {
		while(1) vTaskDelay(10);
	}

	// Create No Split Ring Buffer 
	// One write consumes 8 bytes of header area.
	xRingbuffer1 = xRingbufferCreate((MAX_HTTP_RECV_BUFFER+8)*2, RINGBUF_TYPE_NOSPLIT);
	xRingbuffer2 = xRingbufferCreate((MAX_HTTP_RECV_BUFFER+8)*100, RINGBUF_TYPE_NOSPLIT);
	// Create Allow_Split Ring Buffer
	//xRingbuffer2 = xRingbufferCreate((MAX_HTTP_RECV_BUFFER+8)*10, RINGBUF_TYPE_ALLOWSPLIT);
	configASSERT( xRingbuffer1 );
	configASSERT( xRingbuffer2 );

	// Create Eventgroup
	xEventGroup = xEventGroupCreate();
	configASSERT( xEventGroup );

	xTaskCreate(&vs1053_task, "VS1053", 1024*8, NULL, 5, NULL);
	xTaskCreate(&console_task, "CONSOLE", 1024*4, NULL, 3, NULL);
	xTaskCreate(&client_task, "CLIENT", 1024*8, NULL, 3, NULL);

	// Restart client task, if it stop.
	while(1) {
		xEventGroupWaitBits( xEventGroup,
			HTTP_CLOSE_BIT,	/* The bits within the event group to wait for. */
			pdTRUE,			/* HTTP_CLOSE_BIT should be cleared before returning. */
			pdFALSE,		/* Don't wait for both bits, either bit will do. */
			portMAX_DELAY);	/* Wait forever. */
		ESP_LOGI(TAG, "HTTP_CLOSE_BIT");
		xTaskCreate(&client_task, "CLIENT", 1024*8, NULL, 3, NULL);
		xEventGroupSetBits( xEventGroup, HTTP_RESUME_BIT );
	}

}
