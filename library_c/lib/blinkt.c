#include <gpiod.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <linux/types.h>
#include <signal.h>
#include <unistd.h>
#include "blinkt.h"

#define APA_SOF 0b11100000

#define DEFAULT_BRIGHTNESS 7
#define NUM_LEDS 8

#define MOSI 23
#define SCLK 24

// GPIO chip and line request for libgpiod v2
static struct gpiod_chip *chip = NULL;
static struct gpiod_line_request *request = NULL;

#ifdef TEST
volatile int running = 0;
#endif

int x;

uint32_t leds[NUM_LEDS] = {};

#ifdef TEST
void sigint_handler(int dummy){
	running = 0;
	return;
}
#endif

void clear(){
	for(x = 0; x < NUM_LEDS; x++){
		leds[x] = DEFAULT_BRIGHTNESS;
	}	
}

void set_pixel(uint8_t led, uint8_t r, uint8_t g, uint8_t b){
	if(led >= NUM_LEDS) return;

	leds[led] = rgbb(r,g,b,leds[led] & 0b11111);
}

void set_pixel_brightness(uint8_t led, uint8_t brightness){
	if(led >= NUM_LEDS) return;
	
	leds[led] = (leds[led] & 0xFFFFFF00) | (brightness & 0b11111);
}

void set_pixel_uint32(uint8_t led, uint32_t color){
	if(led >= NUM_LEDS) return;

	leds[led] = color;
}

uint32_t rgbb(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness){
	uint32_t result = 0;
	result = (brightness & 0b11111);
	result |= ((uint32_t)r << 24);
	result |= ((uint32_t)g << 16);
	result |= ((uint16_t)b << 8);
	return result;
}

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b){
	return rgbb(r, g, b, DEFAULT_BRIGHTNESS);
}

inline static void write_byte(uint8_t byte){
	int n;
	for(n = 0; n < 8; n++){
		gpiod_line_request_set_value(request, MOSI, (byte & (1 << (7-n))) > 0);
		gpiod_line_request_set_value(request, SCLK, 1);
		gpiod_line_request_set_value(request, SCLK, 0);
	}

}

void show(void){
	write_byte(0);
	write_byte(0);
	write_byte(0);
	write_byte(0);
	for(x = 0; x < NUM_LEDS; x++){
		write_byte(APA_SOF | (leds[x] & 0b11111));
		write_byte((leds[x] >> 8 ) & 0xFF);
		write_byte((leds[x] >> 16) & 0xFF);
		write_byte((leds[x] >> 24) & 0xFF);
	}
	write_byte(0xff);
	//write_byte(0xff);
	//write_byte(0xff);
	//write_byte(0xff);
}

void stop(void){
	if (request) {
		gpiod_line_request_release(request);
		request = NULL;
	}
	if (chip) {
		gpiod_chip_close(chip);
		chip = NULL;
	}
}


int start(void){
	// Open GPIO chip (gpiochip4 on Raspberry Pi 5, gpiochip0 on older models)
	chip = gpiod_chip_open("/dev/gpiochip4");
	if (!chip) {
		// Fallback to gpiochip0 for older Raspberry Pi models
		chip = gpiod_chip_open("/dev/gpiochip0");
		if (!chip) {
			fprintf(stderr, "Failed to open GPIO chip\n");
			return 1;
		}
	}

#ifdef TEST
	printf("GPIO Initialized\n");
#endif

	// Configure line settings for output
	struct gpiod_line_settings *settings = gpiod_line_settings_new();
	if (!settings) {
		fprintf(stderr, "Failed to create line settings\n");
		gpiod_chip_close(chip);
		chip = NULL;
		return 1;
	}

	if (gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT) < 0) {
		fprintf(stderr, "Failed to set line direction\n");
		gpiod_line_settings_free(settings);
		gpiod_chip_close(chip);
		chip = NULL;
		return 1;
	}

	gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);

	// Configure line request
	struct gpiod_line_config *line_cfg = gpiod_line_config_new();
	if (!line_cfg) {
		fprintf(stderr, "Failed to create line config\n");
		gpiod_line_settings_free(settings);
		gpiod_chip_close(chip);
		chip = NULL;
		return 1;
	}

	unsigned int offsets[2] = {MOSI, SCLK};
	if (gpiod_line_config_add_line_settings(line_cfg, offsets, 2, settings) < 0) {
		fprintf(stderr, "Failed to add line settings\n");
		gpiod_line_config_free(line_cfg);
		gpiod_line_settings_free(settings);
		gpiod_chip_close(chip);
		chip = NULL;
		return 1;
	}

	// Request lines
	struct gpiod_request_config *req_cfg = gpiod_request_config_new();
	if (!req_cfg) {
		fprintf(stderr, "Failed to create request config\n");
		gpiod_line_config_free(line_cfg);
		gpiod_line_settings_free(settings);
		gpiod_chip_close(chip);
		chip = NULL;
		return 1;
	}

	gpiod_request_config_set_consumer(req_cfg, "blinkt");

	request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

	// Clean up config objects
	gpiod_request_config_free(req_cfg);
	gpiod_line_config_free(line_cfg);
	gpiod_line_settings_free(settings);

	if (!request) {
		fprintf(stderr, "Failed to request GPIO lines\n");
		gpiod_chip_close(chip);
		chip = NULL;
		return 1;
	}

	clear();

	return 0;

}

#ifdef TEST
int main(){

	int z;
	int y = 0;

	running = 1;

	signal(SIGINT, sigint_handler);

	if (start()){
		printf("Unable to start apa102\n");
		return 1;
	}

	printf("Running test cycle\n");

	int col = 0;

	while(running){

		for(z = 0; z < NUM_LEDS; z++){		
			switch((col+z) % 4){
				case 0: set_pixel_uint32(z, rgb(y,0,0)); break;
				case 1: set_pixel_uint32(z, rgb(0,y,0)); break;
				case 2: set_pixel_uint32(z, rgb(0,0,y)); break;
				case 3: set_pixel_uint32(z, rgb(y,y,y)); break;
			}
		}

		show();

		usleep(1000);

		y+=1;
                if(y>254) col++;
                col%=4;
		y%=255;

	}


	clear();

	usleep(1000);

	stop();

	return 0;

}
#endif
