#pragma once
#include <stdio.h>
#define ESP_LOGE(tag, ...) do { fprintf(stderr, "%s: ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define ESP_LOGI(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
