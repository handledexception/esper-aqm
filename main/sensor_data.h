#pragma once

#include <stdint.h>

struct sensor_data {
    float    temperature_mcp9808;       // MCP9808 temperature in celsius
    float    mass_concentration_pm1p0;  // PM1.0
    float    mass_concentration_pm2p5;  // PM2.5
    float    mass_concentration_pm4p0;  // PM4.0
    float    mass_concentration_pm10p0; // PM10.0
    float    ambient_humidity;          // SEN55 ambient humidity
    float    ambient_temperature;       // SEN55 ambient temp in celsius
    int16_t  voc_index;                 // SEN55 VOC index (volatile organic chemicals)
    int16_t  nox_index;                 // SEN55 NOX index
};

static inline void sensor_data_init(struct sensor_data* sd)
{
    sd->temperature_mcp9808 = 0.0f;
    sd->mass_concentration_pm1p0 = 0.0f;
    sd->mass_concentration_pm2p5 = 0.0f;
    sd->mass_concentration_pm4p0 = 0.0f;
    sd->mass_concentration_pm10p0 = 0.0f;
    sd->ambient_humidity = 0.0f;
    sd->ambient_temperature = 0.0f;
    sd->voc_index = 0;
    sd->nox_index = 0;
}
