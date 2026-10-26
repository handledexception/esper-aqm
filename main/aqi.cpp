#include "aqi.h"

#include <cmath>

AQI::AQI(Algorithm algo)
: _algo(algo)
{
    initAlgos();
}

const AQI::Breakpoints* AQI::getBreakpoints(Pollutant p)
{
    auto algo = _algos.find(_algo);
    if (algo == _algos.end())
        return nullptr;
    auto bps = algo->second.find(p);
    if (bps == algo->second.end() || bps->second.empty())
        return nullptr;
    return &bps->second;
}

int AQI::GetIntermediateIndex(const Concentration& con)
{
    const Breakpoints* bps = getBreakpoints(con.first);
    if (bps == nullptr)
        return -1;

    // Truncate to the precision the tables are defined at (EPA: 0.1 for PM2.5,
    // whole numbers for PM10). The small epsilon keeps e.g. 12.1 from becoming
    // 12.0 through float error.
    float prec = GetPrecision(con.first);
    float c = std::floor(con.second / prec + 1e-3f) * prec;
    if (c < 0.0f)
        c = 0.0f;

    // Clamp above the top of the table.
    const Breakpoint& top = bps->back();
    if (c >= top.cHi)
        return top.iHi;

    // Use the last row whose low end is <= c. Choosing by the low end alone
    // means a value can never fall into the gap between two rows.
    const Breakpoint* bp = &bps->front();
    for (const Breakpoint& row : *bps) {
        if (c >= row.cLo)
            bp = &row;
    }

    float value = (float)(bp->iHi - bp->iLo) / (bp->cHi - bp->cLo) * (c - bp->cLo) + bp->iLo;
    return (int)std::lround(value);
}

int AQI::GetIndex(const Concentrations& concentrations)
{
    int idx = 0;
    for (const auto& c : concentrations) {
        int iidx = GetIntermediateIndex(c);
        if (iidx > idx)
            idx = iidx;
    }
    return idx;
}

float AQI::GetMaxConcentration(Pollutant p)
{
    const Breakpoints* bps = getBreakpoints(p);
    return bps ? bps->back().cHi : 0.0f;
}

float AQI::GetPrecision(Pollutant p)
{
    switch (p) {
    case Pollutant::PM10: return 1.0f;
    case Pollutant::PM25: return 0.1f;
    default:
        return 1.0f;
    }
}

const char* AQI::GetUnits(Pollutant p)
{
    switch (p) {
    case Pollutant::PM10: return "µg/m³";
    case Pollutant::PM25: return "µg/m³";
    default:
        return NULL;
    }
}

void AQI::initAlgos()
{
    _algos.clear();

    // EPA tables, as revised February 2024 (PM2.5 "Good" now tops out at 9.0,
    // and "Hazardous" is a single 301-500 band).
    BreakpointsMap epa;
    epa[Pollutant::PM10] = {
        {  0.0f,  54.0f,   0,  50},
        { 55.0f, 154.0f,  51, 100},
        {155.0f, 254.0f, 101, 150},
        {255.0f, 354.0f, 151, 200},
        {355.0f, 424.0f, 201, 300},
        {425.0f, 604.0f, 301, 500},
    };
    epa[Pollutant::PM25] = {
        {  0.0f,   9.0f,   0,  50},
        {  9.1f,  35.4f,  51, 100},
        { 35.5f,  55.4f, 101, 150},
        { 55.5f, 125.4f, 151, 200},
        {125.5f, 225.4f, 201, 300},
        {225.5f, 325.4f, 301, 500},
    };
    _algos[Algorithm::EPA] = epa;

    // China MEP tables.
    BreakpointsMap mep;
    mep[Pollutant::PM10] = {
        {  0.0f,  50.0f,   0,  50},
        { 51.0f, 150.0f,  51, 100},
        {151.0f, 250.0f, 101, 150},
        {251.0f, 350.0f, 151, 200},
        {351.0f, 420.0f, 201, 300},
        {421.0f, 500.0f, 301, 400},
        {501.0f, 600.0f, 401, 500},
    };
    mep[Pollutant::PM25] = {
        {  0.0f,  35.0f,   0,  50},
        { 36.0f,  75.0f,  51, 100},
        { 76.0f, 115.0f, 101, 150},
        {116.0f, 150.0f, 151, 200},
        {151.0f, 250.0f, 201, 300},
        {251.0f, 350.0f, 301, 400},
        {351.0f, 500.0f, 401, 500},
    };
    _algos[Algorithm::MEP] = mep;
}
