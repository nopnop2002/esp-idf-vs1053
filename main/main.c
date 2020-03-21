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
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

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
	//setVolume(&dev, 100);
	ESP_LOGI(pcTaskGetTaskName(0), "CONFIG_VOLUME=%d", CONFIG_VOLUME);
	setVolume(&dev, CONFIG_VOLUME);

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
			vTaskDelay(1);
		}
		//vTaskDelay(10);
	}

	// never reach here
	free(buffer);
	ESP_LOGI(pcTaskGetTaskName(0), "Finish");
	vTaskDelete(NULL);
}

typedef struct {
	size_t headerSize;
	char   *headerBuffer;
	size_t icyMetaintSize;
	char   *icyMetaintBuffer;
} HEADER_t;


uint16_t saveHeader(HEADER_t * header, char * buf, int length) {
	if (header->headerBuffer == NULL) {
		header->headerBuffer = malloc(length+1);
		if (header->headerBuffer== NULL) {
			ESP_LOGE(TAG, "headerBuffer malloc fail");
			return 0;
		}
		memcpy(header->headerBuffer, buf, length);
		header->headerSize = length;
		header->headerBuffer[header->headerSize] = 0;
	} else {
		char * tmp;
		tmp = realloc(header->headerBuffer, header->headerSize+length+1);
		if (tmp == NULL) {
			ESP_LOGE(TAG, "headerBuffer realloc fail");
			return 0;
		}
		header->headerBuffer = tmp;
		memcpy(&header->headerBuffer[header->headerSize], buf, length);
		header->headerSize = header->headerSize + length;
		header->headerBuffer[header->headerSize] = 0;
	}
	return header->headerSize;
}

uint16_t getIcyMetaint(HEADER_t * header) {
	char *sp1 = strstr(header->headerBuffer, "\r\nicy-metaint");
	//printf("sp1=%p\n",sp1);
	if (sp1 == NULL) return 0;

	char *sp2 = strstr(sp1+2, "\r\n");
	//printf("sp2=%p\n",sp2);

	header->icyMetaintSize = sp2-sp1-2;
	ESP_LOGI(TAG, "getIcyMetaint:icyMetaintSize=%d",header->icyMetaintSize);
	header->icyMetaintBuffer = malloc(header->icyMetaintSize+1);
	if (header->icyMetaintBuffer == NULL) {
		ESP_LOGE(TAG, "icyMetaintBuffer malloc fail");
		return 0;
	}
	strncpy(header->icyMetaintBuffer, sp1+2, header->icyMetaintSize);
	header->icyMetaintBuffer[header->icyMetaintSize] = 0;
	ESP_LOGI(TAG, "icyMetaintSize=%d",header->icyMetaintSize);
	ESP_LOGI(TAG, "icyMetaintBuffer=[%s]",header->icyMetaintBuffer);

	uint16_t rval = 0;
	char * sp3 = strstr(header->icyMetaintBuffer, ":");
	if (sp3 != NULL) {
		rval = strtol(sp3+1, NULL, 10);
		ESP_LOGI(TAG, "rval=%d",rval);
	}
	return rval;
} 

uint16_t readHeader(int fd, HEADER_t * header) {
	header->headerSize = 0;
	header->headerBuffer = NULL;
	int index = 0;
	char buffer[128];
	int flag = 0;

	while(1) {
		// read data
		int read_len = read(fd, &buffer[index], 1);
		if (read_len == 0) continue;
		if (read_len < 0) {
			ESP_LOGW(pcTaskGetTaskName(0), "read_len = %d", read_len);
			ESP_LOGW(pcTaskGetTaskName(0), "errno = %d", errno);
			break;
		}
		
		if (buffer[index] == 0x0D) {
			flag++;
		} else if (buffer[index] == 0x0A) {
			flag++;
		} else {
			flag = 0;
		}

		index++;
		if (flag == 4) {
			saveHeader(header, buffer, index);
			break;
		}
		if (index == 128) {
			saveHeader(header, buffer, index);
			index = 0;
		}
	}
	return header->headerSize;
}


typedef struct {
	size_t currentSize;				// Total number of stream data
	size_t metaintSize;				// icy-Metaint bytes
	size_t ringbufferSize;
	size_t metadataSize;
	char   *metadata;				// Buffer of metadata
	size_t streamdataSize;			// Byte length of stream data
	char   *streamdata;				// Buffer of streamdata
	char   *StreamTitle;
	char   *StreamUrl;
} METADATA_t;

#define	METADATA	100
#define	STREAMDATA	200
#define	READFAIL	900
#define	MALLOCFAIL	910

int readStremDataWithMetadata(int fd, METADATA_t * hoge) {
	char buffer[1];
	while(1) {
		int read_len = read(fd, buffer, 1);
		if (read_len < 0) {
			// I don't know why it is disconnected from the server.
			ESP_LOGW(pcTaskGetTaskName(0), "read_len = %d", read_len);
			ESP_LOGW(pcTaskGetTaskName(0), "errno = %d", errno);
			return READFAIL;
		}

		if (hoge->metaintSize == 0) {
			hoge->streamdata[hoge->streamdataSize] = buffer[0];
			hoge->streamdataSize++;
			if (hoge->streamdataSize == hoge->ringbufferSize) return STREAMDATA;

		} else {
			if (hoge->currentSize == hoge->metaintSize) {
				hoge->currentSize = 0;
				hoge->metadataSize = buffer[0] * 16;
				ESP_LOGI(TAG, "buffer=%x metadataSize=%d",buffer[0], hoge->metadataSize);
				if (hoge->metadataSize == 0) continue;

				if (hoge->metadata == NULL) {
					hoge->metadata = malloc(hoge->metadataSize+1);
					if (hoge->metadata == NULL) {
						ESP_LOGE(TAG, "malloc fail");
						return MALLOCFAIL;
					}
				} else {
					char *tmp = realloc( hoge->metadata, hoge->metadataSize+1);
					if (tmp == NULL) {
						ESP_LOGE(TAG, "realloc fail");
						return MALLOCFAIL;
					}
					hoge->metadata = tmp;
				}

				for (int i=0; i<hoge->metadataSize; i++) {
					int read_len = read(fd, buffer, 1);
					if (read_len < 0) {
						// I don't know why it is disconnected from the server.
						ESP_LOGW(pcTaskGetTaskName(0), "read_len = %d", read_len);
						ESP_LOGW(pcTaskGetTaskName(0), "errno = %d", errno);
						return READFAIL;
					}
					hoge->metadata[i] = buffer[0];
					hoge->metadata[i+1] = 0;
				}
				return METADATA;

			} else {
				hoge->streamdata[hoge->streamdataSize] = buffer[0];
				hoge->streamdataSize++;
				hoge->currentSize++;
				if (hoge->streamdataSize == hoge->ringbufferSize) return STREAMDATA;
			}

		}
	} // end while
}


void HexDump(char * buff, uint8_t len) {
	int loop = (len + 9) / 10;
	uint8_t index = 0;
	char str[20];
	printf("\n");
	for (int i=0;i<loop;i++) {
		for(int j=0;j<10;j++) {
			if (index < len) {
				if (buff[index] < 0x10) printf("0");
				printf("%x", buff[index]);
				printf(" ");
				if (buff[index] >= 0x20 && buff[index] <= 0x7F) {
					str[j] = buff[index]; 
				} else {
					str[j] = 0x20;
				}
				str[j+1] = 0;
				index++;
			}
		}
		printf("\n");
		printf("%s\n",str);
		delay(1);
	}
}


uint16_t getStreamTitle(METADATA_t * hoge) {
	ESP_LOGI(TAG, "getStreamTitle:metadataSize=%d",hoge->metadataSize);
	char *sp1 = strstr(hoge->metadata, "StreamTitle=");
	if (sp1 == NULL) return 0;
	char *sp2 = strstr(sp1, ";");
	if (sp2 == NULL) return 0;
	uint16_t len = sp2 - sp1 - 12;
	ESP_LOGI(TAG, "StreamTitle len=%d",len);
	hoge->StreamTitle = malloc(len);
	if (hoge->StreamTitle ==NULL) return 0;
	strncpy(hoge->StreamTitle, sp1+12,  len);
	ESP_LOGI(TAG, "StreamTitle=[%s]",hoge->StreamTitle);
	return len;
}

uint16_t getStreamUrl(METADATA_t * hoge) {
	ESP_LOGI(TAG, "getStreamUrl:metadataSize=%d",hoge->metadataSize);
	char *sp1 = strstr(hoge->metadata, "StreamUrl=");
	if (sp1 == NULL) return 0;
	char *sp2 = strstr(sp1, ";");
	if (sp2 == NULL) return 0;
	uint16_t len = sp2 - sp1 - 10;
	ESP_LOGI(TAG, "StreamUrl len=%d",len);
	hoge->StreamUrl = malloc(len);
	if (hoge->StreamUrl ==NULL) return 0;
	strncpy(hoge->StreamUrl, sp1+10,  len);
	ESP_LOGI(TAG, "StreamUrl=[%s]",hoge->StreamUrl);
	return len;
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
			HTTP_RESUME_BIT,	/* The bits within the event group to wait for. */
			pdTRUE,				/* HTTP_RESUME_BIT should be cleared before returning. */
			pdFALSE,			/* Don't wait for both bits, either bit will do. */
			portMAX_DELAY);		/* Wait forever. */
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
	//sprintf(buffer, "GET %s HTTP/1.0\r\n", SERVER_PATH);
	ret = send(fd, buffer, strlen(buffer), 0);
	LWIP_ASSERT("ret == strlen(buffer)", ret == strlen(buffer));

	sprintf(buffer, "HOST: %s\r\n", SERVER_HOST);
	ret = send(fd, buffer, strlen(buffer), 0);
	LWIP_ASSERT("ret == strlen(buffer)", ret == strlen(buffer));

	sprintf(buffer, "User-Agent: ESP32/1.00\r\n");
	ret = send(fd, buffer, strlen(buffer), 0);
	LWIP_ASSERT("ret == strlen(buffer)", ret == strlen(buffer));

	// receive icy-metadata
	// https://stackoverflow.com/questions/44050266/get-info-from-streaming-radio
	sprintf(buffer, "Icy-MetaData: 1\r\n");
	//sprintf(buffer, "Icy-MetaData: 0\r\n");
	ret = send(fd, buffer, strlen(buffer), 0);
	LWIP_ASSERT("ret == strlen(buffer)", ret == strlen(buffer));

	sprintf(buffer, "Connection: close\r\n");
	ret = send(fd, buffer, strlen(buffer), 0);
	LWIP_ASSERT("ret == strlen(buffer)", ret == strlen(buffer));

	sprintf(buffer, "\r\n");
	ret = send(fd, buffer, strlen(buffer), 0);
	LWIP_ASSERT("ret == strlen(buffer)", ret == strlen(buffer));

	free(buffer);

#if 0
	// set timeout
	struct timeval timeout;
	timeout.tv_usec = 0;
	timeout.tv_sec = 3;
	setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
#endif

	// read HTTP header
	// SHOUTcast server does not return header length.
	// Therefore, it is necessary to find the end of the header.
	HEADER_t header;
	readHeader(fd, &header);
	ESP_LOGI(pcTaskGetTaskName(0), "headerBuffer=[%s]",header.headerBuffer);
	ESP_LOGI(pcTaskGetTaskName(0), "headerSize=%d",header.headerSize);

#if 0
HTTP/1.0 200 OK
Content-Type: audio/mpeg
Date: Wed, 18 Mar 2020 11:45:45 GMT
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
icy-metaint:16000
#endif

	uint16_t metaint = getIcyMetaint(&header);
	ESP_LOGI(pcTaskGetTaskName(0), "metaint=%d", metaint);

	METADATA_t meta;
	meta.currentSize = 0;
	meta.metaintSize = metaint;
	meta.ringbufferSize = MAX_HTTP_RECV_BUFFER;
	meta.metadataSize = 0;
	meta.metadata = NULL;
	meta.streamdataSize = 0;
	meta.streamdata = malloc(MAX_HTTP_RECV_BUFFER);
	meta.StreamTitle = NULL;
	meta.StreamUrl = NULL;
	if (meta.streamdata == NULL) {
		ESP_LOGE(pcTaskGetTaskName(0), "streamdata malloc fail");
	}

	// main loop
	while(1) {
		int type = readStremDataWithMetadata(fd, &meta);
		if (type == READFAIL) break;
		if (type == MALLOCFAIL) break;

		if (type == METADATA) {
			ESP_LOGI(pcTaskGetTaskName(0),"metadataSize=%d metadata=[%s]",meta.metadataSize, meta.metadata); 
			xRingbufferSend(xRingbuffer1, meta.metadata, meta.metadataSize, 0);
		}

		if (type == STREAMDATA) {
			ESP_LOGD(pcTaskGetTaskName(0),"streamdataSize=%d ringbufferSize=%d",meta.streamdataSize, meta.ringbufferSize);
			xRingbufferSend(xRingbuffer2, meta.streamdata, meta.streamdataSize, 0);
			meta.streamdataSize = 0;

			// Adjust the buffer size according to the free size of xRingbuffer.
			size_t freeSize = xRingbufferGetCurFreeSize(xRingbuffer2);
			meta.ringbufferSize = MAX_HTTP_RECV_BUFFER;
			if (freeSize < MAX_HTTP_RECV_BUFFER*2) {
				meta.ringbufferSize = MIN_HTTP_RECV_BUFFER;
				vTaskDelay(1);
			}
#if 0
			meta.ringbufferSize = MIN_HTTP_RECV_BUFFER;
			if (freeSize > MAX_HTTP_RECV_BUFFER*2) meta.ringbufferSize = MAX_HTTP_RECV_BUFFER;
#endif
			ESP_LOGD(pcTaskGetTaskName(0), "freeSize=%d ringbufferSize=%d", freeSize, meta.ringbufferSize);
		}
	}

	// release memory
	free(meta.streamdata);
	if (meta.metadata) free(meta.metadata);

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
	// https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/system/mem_alloc.html
	// Due to a technical limitation, the maximum statically allocated DRAM usage is 160KB.
	// The remaining 160KB (for a total of 320KB of DRAM) can only be allocated at runtime as heap.
	xRingbuffer1 = xRingbufferCreate(1000, RINGBUF_TYPE_NOSPLIT);
	xRingbuffer2 = xRingbufferCreate(100000, RINGBUF_TYPE_NOSPLIT);
	configASSERT( xRingbuffer1 );
	configASSERT( xRingbuffer2 );

	// Create Eventgroup
	xEventGroup = xEventGroupCreate();
	configASSERT( xEventGroup );

	xTaskCreate(&vs1053_task, "VS1053", 1024*8, NULL, 6, NULL);
	xTaskCreate(&console_task, "CONSOLE", 1024*4, NULL, 3, NULL);
	xTaskCreate(&client_task, "CLIENT", 1024*8, NULL, 5, NULL);

	// Restart client task, if it stop.
	while(1) {
		xEventGroupWaitBits( xEventGroup,
			HTTP_CLOSE_BIT,		/* The bits within the event group to wait for. */
			pdTRUE,				/* HTTP_CLOSE_BIT should be cleared before returning. */
			pdFALSE,			/* Don't wait for both bits, either bit will do. */
			portMAX_DELAY);		/* Wait forever. */
		ESP_LOGI(TAG, "HTTP_CLOSE_BIT");
		xTaskCreate(&client_task, "CLIENT", 1024*8, NULL, 5, NULL);
		xEventGroupSetBits( xEventGroup, HTTP_RESUME_BIT );
	}

}
