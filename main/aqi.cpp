#include "aqi.h"

AQI::AQI(Algorithm algo)
: _algo(algo)
{
    initIndices();
    initAlgos();
}

int AQI::GetIntermediateIndex(const Concentration& con)
{
    if (_algos.find(_algo) == _algos.end()) {
        return -1;
    }

    // Get pollutant-to-breakpoints map for specified algorithm
    BreakpointsMap breakpointsMap = _algos[_algo];
    Pollutant pollutant = std::get<0>(con);
    if (breakpointsMap.find(pollutant) == breakpointsMap.end()) {
        return -2;
    }

    // Get breakpoints for specified pollutant
    Breakpoints breakpoints = breakpointsMap[pollutant];
    float concentration = std::get<1>(con);
    float bpLo = 0.0f;
    float bpHi = 1.0f;
    int idx = 0;
    for (auto bp : breakpoints) {
        if (concentration >= std::get<0>(bp) && concentration <= std::get<1>(bp)) {
            bpLo = std::get<0>(bp);
            bpHi = std::get<1>(bp);
            break;
        }
        idx++;
    }

    // Get air quality index
    if (idx >= _indices.size()) {
        idx = _indices.size()-1;
    }
    auto aqi = _indices[idx];
    float aqiLo = std::get<0>(aqi);
    float aqiHi = std::get<1>(aqi);
    float value = (aqiHi - aqiLo) / (bpHi - bpLo) * (concentration - bpLo) + aqiLo;

    return static_cast<int>(value);
}

int AQI::GetIndex(const Concentrations& concentrations)
{
    int idx = 0;
    for (auto c : concentrations) {
        int iidx = GetIntermediateIndex(c);
        if (iidx > idx)
            idx = iidx;
    }
    return idx;
}

float AQI::GetConcentration(int intermediate)
{
    float cons = 0.0f;
    return cons;
}

float AQI::GetMaxConcentration(Pollutant p)
{
    if (_algos.find(_algo) == _algos.end()) {
        return 0.0f;
    }

    // Get pollutant-to-breakpoints map for specified algorithm
    BreakpointsMap breakpointsMap = _algos[_algo];
    if (breakpointsMap.find(p) == breakpointsMap.end()) {
        return 0.0f;
    }

    // Get breakpoints for specified pollutant
    Breakpoints breakpoints = breakpointsMap[p];
    auto bp = breakpoints[breakpoints.size()-1];
    return std::get<1>(bp);
}

float AQI::GetPrecision(Pollutant p)
{
    switch (p) {
    case Pollutant::PM10: return 1.0f;
    case Pollutant::PM25: return 0.1f;
    default:
        return 1.0f;
    }

    return 1.0f;
}

const char* AQI::GetUnits(Pollutant p)
{
    switch (p) {
    case Pollutant::PM10: return "µg/m³";
    case Pollutant::PM25: return "µg/m³";
    default:
        return NULL;
    }

    return NULL;
}

void AQI::initIndices()
{
    // From EPA AQI guidelines: https://www.govinfo.gov/content/pkg/FR-2013-01-15/pdf/2012-30946.pdf
    // Breakpoints are the same for EPA and MEP algorithms.
    _indices.clear();
    _indices.push_back(Breakpoint{0.0f, 50.0f});
    _indices.push_back(Breakpoint{51.0f, 100.0f});
    _indices.push_back(Breakpoint{101.0f, 150.0f});
    _indices.push_back(Breakpoint{151.0f, 200.0f});
    _indices.push_back(Breakpoint{201.0f, 300.0f});
    _indices.push_back(Breakpoint{301.0f, 400.0f});
    _indices.push_back(Breakpoint{401.0f, 500.0f});
}

void AQI::initAlgos()
{
    _algos.clear();

    // EPA algorithm data
    BreakpointsMap epaBreakpoints;
    // PM10
    Breakpoints epa_bp_pm10 = {
        {0.0f, 54.0f},
        {55.0f, 154.0f},
        {155.0f, 254.0f},
        {255.0f, 354.0f},
        {355.0f, 424.0f},
        {425.0f, 504.0f},
        {505.0f, 604.0f},
    };
    epaBreakpoints.emplace(BreakpointEntry{
       Pollutant::PM10, epa_bp_pm10
    });

    // PM2.5
    Breakpoints epa_bp_pm25 = {
        {0.0f, 12.0f},
        {12.1f, 35.4f},
        {35.5f, 55.4f},
        {55.5f, 150.4f},
        {150.5f, 250.4f},
        {250.5f, 350.4f},
        {350.5f, 500.4f},
    };
    epaBreakpoints.emplace(BreakpointEntry{
        Pollutant::PM25, epa_bp_pm25
    });
    _algos.emplace(std::pair<Algorithm, BreakpointsMap>{Algorithm::EPA, epaBreakpoints});

    // MEP algorithm data
    BreakpointsMap mepBreakpoints;
    // PM10
    Breakpoints mep_bp_pm10 = {
        {0.0f, 50.0f},
        {51.0f, 150.0f},
        {151.0f, 250.0f},
        {251.0f, 350.0f},
        {351.0f, 420.0f},
        {421.0f, 500.0f},
        {501.0f, 600.0f},
    };
    mepBreakpoints.emplace(BreakpointEntry{
        Pollutant::PM10, mep_bp_pm10
    });
    // PM2.5
    Breakpoints mep_bp_pm25 = {
        {0.0f, 35.0f},
        {36.0f, 75.0f},
        {76.0f, 115.0f},
        {116.0f, 150.0f},
        {151.0f, 250.0f},
        {251.0f, 350.0f},
        {351.0f, 500.0f,}
    };
    mepBreakpoints.emplace(BreakpointEntry{
        Pollutant::PM25, mep_bp_pm25
    });
    _algos.emplace(std::pair<Algorithm, BreakpointsMap>{Algorithm::MEP, mepBreakpoints});
}
