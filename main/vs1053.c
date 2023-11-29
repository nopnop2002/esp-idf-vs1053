/**
 * This is a driver library for VS1053 MP3 Codec Breakout
 * (Ogg Vorbis / MP3 / AAC / WMA / FLAC / MIDI Audio Codec Chip).
 * Adapted for Espressif ESP32 boards.
 *
 * version 1.0.1
 *
 * Licensed under GNU GPLv3 <http://gplv3.fsf.org/>
 * Copyright 2018
 *
 * @authors baldram, edzelf, MagicCube, maniacbug
 * @authors nopnop2002
 *
 * Development log:
 *	- 2011: initial VS1053 Arduino library
 *			originally written by J. Coliz (github: @maniacbug),
 *	- 2016: refactored and integrated into Esp-radio sketch
 *			by Ed Smallenburg (github: @edzelf)
 *	- 2017: refactored to use as PlatformIO library
 *			by Marcin Szalomski (github: @baldram | twitter: @baldram)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License or later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <driver/spi_master.h>
#include <driver/gpio.h>
#include "esp_log.h"

#include "vs1053.h"

#define TAG "VS1053"
#define _DEBUG_ 0

static const int GPIO_SCLK = 18;
static const int GPIO_MISO = 19;
static const int GPIO_MOSI = 23;

//static const int SPI_Frequency = SPI_MASTER_FREQ_20M;
//static const int SPI_Frequency = SPI_MASTER_FREQ_26M;
//static const int SPI_Frequency = SPI_MASTER_FREQ_40M;
//static const int SPI_Frequency = SPI_MASTER_FREQ_80M;

void delay(int ms) {
	int _ms = ms + (portTICK_PERIOD_MS - 1);
	TickType_t xTicksToDelay = _ms / portTICK_PERIOD_MS;
	ESP_LOGD(TAG, "ms=%d _ms=%d",ms, _ms);
	ESP_LOGD(TAG, "portTICK_PERIOD_MS=%"PRIu32" xTicksToDelay=%"PRIu32, portTICK_PERIOD_MS, xTicksToDelay);
	vTaskDelay(xTicksToDelay);
}


void spi_master_init(VS1053_t * dev, int16_t GPIO_CS, int16_t GPIO_DCS, int16_t GPIO_DREQ, int16_t GPIO_RESET)
{
	esp_err_t ret;

	ESP_LOGI(TAG, "GPIO_DREQ=%d",GPIO_DREQ);
#if 0
	gpio_pad_select_gpio( GPIO_DREQ );
	gpio_set_direction( GPIO_DREQ, GPIO_MODE_INPUT );
#endif

#if 1
	gpio_config_t gpio_conf;
	gpio_conf.mode = GPIO_MODE_INPUT;
	gpio_conf.pull_up_en =	GPIO_PULLUP_DISABLE;
	gpio_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
	gpio_conf.intr_type = GPIO_INTR_DISABLE;   
	gpio_conf.pin_bit_mask = ((uint64_t)(((uint64_t)1)<<GPIO_DREQ));
	ESP_ERROR_CHECK(gpio_config(&gpio_conf));
#endif


	ESP_LOGI(TAG, "GPIO_CS=%d",GPIO_CS);
	//gpio_pad_select_gpio( GPIO_CS );
	gpio_reset_pin( GPIO_CS );
	gpio_set_direction( GPIO_CS, GPIO_MODE_OUTPUT );
	gpio_set_level( GPIO_CS, 1 );

	ESP_LOGI(TAG, "GPIO_DCS=%d",GPIO_DCS);
	//gpio_pad_select_gpio( GPIO_DCS );
	gpio_reset_pin( GPIO_DCS );
	gpio_set_direction( GPIO_DCS, GPIO_MODE_OUTPUT );
	gpio_set_level( GPIO_DCS, 1 );

	ESP_LOGI(TAG, "GPIO_RESET=%d",GPIO_RESET);
	if ( GPIO_RESET >= 0 ) {
		//gpio_pad_select_gpio( GPIO_RESET );
		gpio_reset_pin( GPIO_RESET );
		gpio_set_direction( GPIO_RESET, GPIO_MODE_OUTPUT );
		gpio_set_level( GPIO_RESET, 0 );
		vTaskDelay( pdMS_TO_TICKS( 100 ) );
		gpio_set_level( GPIO_RESET, 1 );
	}

	spi_bus_config_t buscfg = {
		.sclk_io_num = GPIO_SCLK,
		.mosi_io_num = GPIO_MOSI,
		.miso_io_num = GPIO_MISO,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.flags = SPICOMMON_BUSFLAG_MASTER
	};

	ret = spi_bus_initialize( HSPI_HOST, &buscfg, 1 );
	assert(ret==ESP_OK);

	// Init SPI in slow mode
	//int freq = spi_cal_clock(APB_CLK_FREQ, 1400000, 128, NULL);
	//ESP_LOGI(TAG,"VS1053 LowFreq: %d",freq);
	int freq = 200000;
	spi_device_interface_config_t devcfg={
		.clock_speed_hz = freq,
		.command_bits = 8,
		.address_bits = 8,
		.dummy_bits = 0,
		.duty_cycle_pos = 0,
		.cs_ena_pretrans = 0,
		.cs_ena_posttrans = 1,
		.flags = SPI_DEVICE_NO_DUMMY,
		.mode = 0,
		.spics_io_num = -1,
		.queue_size = 1
	};

	spi_device_handle_t lvsspi;
	ret = spi_bus_add_device( HSPI_HOST, &devcfg, &lvsspi);
	ESP_LOGD(TAG, "spi_bus_add_device=%d",ret);
	assert(ret==ESP_OK);
	delay(20);

	dev->dreq_pin = GPIO_DREQ;
	dev->cs_pin = GPIO_CS;
	dev->dcs_pin = GPIO_DCS;
	dev->reset_pin = GPIO_RESET;
	dev->SPIHandleLow = lvsspi;
	printDetails(dev, "");

	// Init SPI in high mode
	spi_device_handle_t hvsspi;
	if (testComm(dev, "Slow SPI,Testing VS1053 read/write registers...\n")) {
		//softReset();
		// Switch on the analog parts
		write_register(dev, SCI_AUDATA, 44101); // 44.1kHz stereo
		// The next clocksetting allows SPI clocking at 5 MHz, 4 MHz is safe then.
		write_register(dev, SCI_CLOCKF, 6 << 12); // Normal clock settings multiplyer 3.0 = 12.2 MHz
		// SPI Clock to 4 MHz. Now you can set high speed SPI clock.
		//VS1053_SPI = SPISettings(4000000, MSBFIRST, SPI_MODE0);

		//freq =spi_cal_clock(APB_CLK_FREQ, 6100000, 128, NULL);
		//ESP_LOGI(TAG,"VS1053 HighFreq: %d",freq);
		devcfg.clock_speed_hz = 6000000;
		devcfg.command_bits = 0;
		devcfg.address_bits = 0;
		ret = spi_bus_add_device( HSPI_HOST, &devcfg, &hvsspi);
		ESP_LOGD(TAG, "spi_bus_add_device=%d",ret);
		assert(ret==ESP_OK);
		write_register(dev, SCI_MODE, _BV(SM_SDINEW) | _BV(SM_LINE1));
		testComm(dev, "Fast SPI, Testing VS1053 read/write registers again...Takes a little time\n");
		ESP_LOGI(TAG, "testComm end");
		delay(10);
		await_data_request(dev);
		dev->endFillByte = wram_read(dev, 0x1E06) & 0xFF;
		ESP_LOGI(TAG, "endFillByte=%x", dev->endFillByte);
		printDetails(dev, "After last clock setting") ;
		dev->chipVersion = getHardwareVersion(dev);
		ESP_LOGI(TAG, "chipVersion=%x", dev->chipVersion);
		delay(100);
	}

	dev->SPIHandleFast = hvsspi;
}


void await_data_request(VS1053_t * dev)
{
	while (!gpio_get_level(dev->dreq_pin)) {
		taskYIELD();						// Very short delay
	}
}

bool current_data_request(VS1053_t * dev)
{
	return (gpio_get_level(dev->dreq_pin) == HIGH);
}

void control_mode_on(VS1053_t * dev)
{
	gpio_set_level(dev->dcs_pin, HIGH);		   // Bring slave in control mode
	gpio_set_level(dev->cs_pin, LOW);
}

void control_mode_off(VS1053_t * dev) {
	gpio_set_level(dev->cs_pin, HIGH);		   // End control mode
}

void data_mode_on(VS1053_t * dev)
{
	gpio_set_level(dev->cs_pin, HIGH);		   // Bring slave in data mode
	gpio_set_level(dev->dcs_pin, LOW);
}

void data_mode_off(VS1053_t * dev)
{
	gpio_set_level(dev->dcs_pin, HIGH);		   // End data mode
}


uint16_t read_register(VS1053_t * dev, uint8_t _reg)
{
	spi_transaction_t SPITransaction;
	esp_err_t ret;

	control_mode_on(dev);
	await_data_request(dev); // Wait for DREQ to be HIGH
	memset( &SPITransaction, 0, sizeof( spi_transaction_t ) );
	SPITransaction.length=16;
	SPITransaction.flags |= SPI_TRANS_USE_RXDATA	;
	SPITransaction.cmd = VS_READ_COMMAND;
	SPITransaction.addr = _reg;
	ret = spi_device_transmit( dev->SPIHandleLow, &SPITransaction );
	assert(ret==ESP_OK);
	uint16_t result = (((SPITransaction.rx_data[0]&0xFF)<<8) | ((SPITransaction.rx_data[1])&0xFF)) ;
	await_data_request(dev); // Wait for DREQ to be HIGH again
	control_mode_off(dev);
	return result;
}

bool write_register(VS1053_t * dev, uint8_t _reg, uint16_t _value)
{
	spi_transaction_t SPITransaction;
	esp_err_t ret;

	control_mode_on(dev);
	await_data_request(dev); // Wait for DREQ to be HIGH
	memset( &SPITransaction, 0, sizeof( spi_transaction_t ) );
	SPITransaction.flags |= SPI_TRANS_USE_TXDATA;
	SPITransaction.cmd = VS_WRITE_COMMAND;
	SPITransaction.addr = _reg;
	SPITransaction.tx_data[0] = (_value >> 8) & 0xFF;
	SPITransaction.tx_data[1] = (_value & 0xFF);
	SPITransaction.length= 16;
	ret = spi_device_transmit( dev->SPIHandleLow, &SPITransaction );
	assert(ret==ESP_OK);
	await_data_request(dev); // Wait for DREQ to be HIGH again
	control_mode_off(dev);
	return true;
}

bool sdi_send_buffer(VS1053_t * dev, uint8_t* data, size_t len)
{
	size_t chunk_length; // Length of chunk 32 byte or shorter
	spi_transaction_t SPITransaction;
	esp_err_t ret;

	data_mode_on(dev);
	while (len) // More to do?
	{
		await_data_request(dev); // Wait for space available
		chunk_length = len;
		if (len > VS1053_CHUNK_SIZE) {
			chunk_length = VS1053_CHUNK_SIZE;
		}
		len -= chunk_length;
	   
	   
		memset( &SPITransaction, 0, sizeof( spi_transaction_t ) );
		SPITransaction.length = chunk_length * 8;
		SPITransaction.tx_buffer = data;
		ret = spi_device_transmit( dev->SPIHandleFast, &SPITransaction );
		assert(ret==ESP_OK);
		data += chunk_length;
	}
	data_mode_off(dev);
	return true;
}

bool sdi_send_fillers(VS1053_t * dev, size_t len)
{
	size_t chunk_length; // Length of chunk 32 byte or shorter
	spi_transaction_t SPITransaction;
	esp_err_t ret;
	uint8_t data[VS1053_CHUNK_SIZE];
	for(int i=0;i<VS1053_CHUNK_SIZE;i++) data[i] = dev->endFillByte;
	
	data_mode_on(dev);
	while (len) // More to do?
	{
		await_data_request(dev); // Wait for space available
		chunk_length = len;
		if (len > VS1053_CHUNK_SIZE) {
			chunk_length = VS1053_CHUNK_SIZE;
		}
		len -= chunk_length;
	   
	   
		memset( &SPITransaction, 0, sizeof( spi_transaction_t ) );
		SPITransaction.length = chunk_length * 8;
		SPITransaction.tx_buffer = data;
		ret = spi_device_transmit( dev->SPIHandleFast, &SPITransaction );
		assert(ret==ESP_OK);
	}
	data_mode_off(dev);
	return true;

}


void wram_write(VS1053_t * dev, uint16_t address, uint16_t data) {
	write_register(dev, SCI_WRAMADDR, address);
	write_register(dev, SCI_WRAM, data);
}

uint16_t wram_read(VS1053_t * dev, uint16_t address) {
	write_register(dev, SCI_WRAMADDR, address); // Start reading from WRAM
	//uint16_t wk = read_register(dev, SCI_WRAM);		// Read back result
	//ESP_LOGI(TAG, "SCI_WRAM=%x", wk);
	return read_register(dev, SCI_WRAM);		// Read back result
}

bool testComm(VS1053_t * dev, char *header) {
	// Test the communication with the VS1053 module.  The result wille be returned.
	// If DREQ is low, there is problably no VS1053 connected.	Pull the line HIGH
	// in order to prevent an endless loop waiting for this signal.  The rest of the
	// software will still work, but readbacks from VS1053 will fail.
	int i; // Loop control
	uint16_t r1, r2, cnt = 0;
	uint16_t delta = 300; // 3 for fast SPI

	if (!gpio_get_level(dev->dreq_pin)) {
		ESP_LOGW(TAG, "VS1053 not properly installed!");
		// Allow testing without the VS1053 module
		//pinMode(dreq_pin, INPUT_PULLUP); // DREQ is now input with pull-up
		return false;					 // Return bad result
	}
	// Further TESTING.  Check if SCI bus can write and read without errors.
	// We will use the volume setting for this.
	// Will give warnings on serial output if DEBUG is active.
	// A maximum of 20 errors will be reported.
	if (strstr(header, "Fast")) {
		delta = 3; // Fast SPI, more loops
	}

	ESP_LOGI(TAG, "%s", header);  // Show a header

	for (i = 0; (i < 0xFFFF) && (cnt < 20); i += delta) {
		write_register(dev, SCI_VOL, i);		 // Write data to SCI_VOL
		r1 = read_register(dev, SCI_VOL);		 // Read back for the first time
		r2 = read_register(dev, SCI_VOL);		 // Read back a second time
		if (r1 != r2 || i != r1 || i != r2)		 // Check for 2 equal reads
		{
			ESP_LOGW(TAG, "VS1053 error retry SB:%04X R1:%04X R2:%04X", i, r1, r2);
			cnt++;
			delay(10);
		}
		taskYIELD(); // Allow ESP firmware to do some bookkeeping
	}
	return (cnt == 0); // Return the result
}

#if 0
void begin() {
	pinMode(dreq_pin, INPUT); // DREQ is an input
	pinMode(cs_pin, OUTPUT);  // The SCI and SDI signals
	pinMode(dcs_pin, OUTPUT);
	digitalWrite(dcs_pin, HIGH); // Start HIGH for SCI en SDI
	digitalWrite(cs_pin, HIGH);
	delay(100);
	LOG("\n");
	LOG("Reset VS1053...\n");
	digitalWrite(dcs_pin, LOW); // Low & Low will bring reset pin low
	digitalWrite(cs_pin, LOW);
	delay(500);
	LOG("End reset VS1053...\n");
	digitalWrite(dcs_pin, HIGH); // Back to normal again
	digitalWrite(cs_pin, HIGH);
	delay(500);
	// Init SPI in slow mode ( 0.2 MHz )
	VS1053_SPI = SPISettings(200000, MSBFIRST, SPI_MODE0);
	// printDetails("Right after reset/startup");
	delay(20);
	// printDetails("20 msec after reset");
	if (testComm("Slow SPI,Testing VS1053 read/write registers...")) {
		//softReset();
		// Switch on the analog parts
		write_register(SCI_AUDATA, 44101); // 44.1kHz stereo
		// The next clocksetting allows SPI clocking at 5 MHz, 4 MHz is safe then.
		write_register(SCI_CLOCKF, 6 << 12); // Normal clock settings multiplyer 3.0 = 12.2 MHz
		// SPI Clock to 4 MHz. Now you can set high speed SPI clock.
		VS1053_SPI = SPISettings(4000000, MSBFIRST, SPI_MODE0);
		write_register(SCI_MODE, _BV(SM_SDINEW) | _BV(SM_LINE1));
		testComm("Fast SPI, Testing VS1053 read/write registers again...");
		delay(10);
		await_data_request();
		endFillByte = wram_read(0x1E06) & 0xFF;
		LOG("endFillByte is %X\n", endFillByte);
		//printDetails("After last clocksetting") ;
		delay(100);
	}
}
#endif

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void setVolume(VS1053_t * dev, uint8_t vol) {
	// Set volume.	Both left and right.
	// Input value is 0..100.  100 is the loudest.
	uint16_t value; // Value to send to SCI_VOL

	dev->curvol = vol;						  // Save for later use
	value = map(vol, 0, 100, 0xFF, 0x00); // 0..100% to one channel
	value = (value << 8) | value;
	write_register(dev, SCI_VOL, value); // Volume left and right
}

void setTone(VS1053_t * dev, uint8_t *rtone) { // Set bass/treble (4 nibbles)
	// Set tone characteristics.  See documentation for the 4 nibbles.
	uint16_t value = 0; // Value to send to SCI_BASS
	int i;				// Loop control

	for (i = 0; i < 4; i++) {
		value = (value << 4) | rtone[i]; // Shift next nibble in
	}
	write_register(dev, SCI_BASS, value); // Volume left and right
}

uint8_t getVolume(VS1053_t * dev) { // Get the currenet volume setting.
	return dev->curvol;
}

void startSong(VS1053_t * dev) {
	sdi_send_fillers(dev, 10);
}

void playChunk(VS1053_t * dev, uint8_t *data, size_t len) {
	sdi_send_buffer(dev, data, len);
}

void stopSong(VS1053_t * dev) {
	uint16_t modereg; // Read from mode register
	int i;			  // Loop control

	sdi_send_fillers(dev, 2052);
	delay(10);
	write_register(dev, SCI_MODE, _BV(SM_SDINEW) | _BV(SM_CANCEL));
	for (i = 0; i < 200; i++) {
		sdi_send_fillers(dev, 32);
		modereg = read_register(dev, SCI_MODE); // Read status
		if ((modereg & _BV(SM_CANCEL)) == 0) {
			sdi_send_fillers(dev, 2052);
			ESP_LOGI(TAG, "Song stopped correctly after %d msec", i * 10);
			return;
		}
		delay(10);
	}
	printDetails(dev, "Song stopped incorrectly!");
}

void softReset(VS1053_t * dev) {
	ESP_LOGI(TAG, "Performing soft-reset");
	write_register(dev, SCI_MODE, _BV(SM_SDINEW) | _BV(SM_RESET));
	delay(10);
	await_data_request(dev);
}

void printDetails(VS1053_t * dev, char *header) {
	//uint16_t regbuf[SCI_num_registers];
	//uint8_t i;

	ESP_LOGI(TAG, "%s", header);
	ESP_LOGI(TAG, "REG	 Contents");
	ESP_LOGI(TAG, "---	 -----");
	for (int i = 0; i <= SCI_num_registers; i++) {
		//regbuf[i] = read_register(dev, i);
		uint16_t regbuf = read_register(dev, i);
		delay(5);
		//ESP_LOGI(TAG, "%3X - %5X", i, regbuf[i]);
		ESP_LOGI(TAG, "%3X - %5X", i, regbuf);
	}
#if 0
	for (i = 0; i <= SCI_num_registers; i++) {
		regbuf[i] = read_register(dev, i);
	}
	for (i = 0; i <= SCI_num_registers; i++) {
		delay(5);
		ESP_LOGI(TAG, "%3X - %5X", i, regbuf[i]);
	}
#endif
}

/**
 * An optional switch.
 * Most VS1053 modules will start up in MIDI mode. The result is that there is no audio when playing MP3.
 * You can modify the board, but there is a more elegant way without soldering.
 * No side effects for boards which do not need this switch. It means you can call it just in case.
 *
 * Read more here: http://www.bajdi.com/lcsoft-vs1053-mp3-module/#comment-33773
 */
void switchToMp3Mode(VS1053_t * dev) {
	wram_write(dev, 0xC017, 3); // GPIO DDR = 3
	wram_write(dev, 0xC019, 0); // GPIO ODATA = 0
	delay(100);
	ESP_LOGI(TAG, "Switched to mp3 mode");
	softReset(dev);
}

/**
 * A lightweight method to check if VS1053 is correctly wired up (power supply and connection to SPI interface).
 *
 * @return true if the chip is wired up correctly
 */
bool isChipConnected(VS1053_t * dev) {
	uint16_t status = read_register(dev, SCI_STATUS);

	return !(status == 0 || status == 0xFFFF);
}

/**
 * Provides current decoded time in full seconds (from SCI_DECODE_TIME register value)
 *
 * When decoding correct data, current decoded time is shown in SCI_DECODE_TIME
 * register in full seconds. The user may change the value of this register.
 * In that case the new value should be written twice to make absolutely certain
 * that the change is not overwritten by the firmware. A write to SCI_DECODE_TIME
 * also resets the byteRate calculation.
 *
 * SCI_DECODE_TIME is reset at every hardware and software reset. It is no longer
 * cleared when decoding of a file ends to allow the decode time to proceed
 * automatically with looped files and with seamless playback of multiple files.
 * With fast playback (see the playSpeed extra parameter) the decode time also
 * counts faster. Some codecs (WMA and Ogg Vorbis) can also indicate the absolute
 * play position, see the positionMsec extra parameter in section 10.11.
 *
 * @see VS1053b Datasheet (1.31) / 9.6.5 SCI_DECODE_TIME (RW)
 *
 * @return current decoded time in full seconds
 */
uint16_t getDecodedTime(VS1053_t * dev) {
	return read_register(dev, SCI_DECODE_TIME);
}

/**
 * Clears decoded time (sets SCI_DECODE_TIME register to 0x00)
 *
 * The user may change the value of this register. In that case the new value
 * should be written twice to make absolutely certain that the change is not
 * overwritten by the firmware. A write to SCI_DECODE_TIME also resets the
 * byteRate calculation.
 */
void clearDecodedTime(VS1053_t * dev) {
	write_register(dev, SCI_DECODE_TIME, 0x00);
	write_register(dev, SCI_DECODE_TIME, 0x00);
} 

uint8_t getHardwareVersion(VS1053_t * dev) {
	uint16_t status = read_register(dev, SCI_STATUS);

	return (status>>4)&0xf;
}
