/*
Portions of this code based on the "python-aqi" package.

python-aqi Copyright (c) 2014, Stefan "hr" Berder
Copyright (c) 2014, Stefan "hr" Berder
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <map>
#include <vector>

class AQI {
public:
    enum class Pollutant {
        None,
        PM10,
        PM25
    };

    enum class Algorithm {
        None,
        EPA,
        MEP
    };

    // One row of an AQI table: concentrations [cLo, cHi] map linearly onto
    // index values [iLo, iHi].
    struct Breakpoint {
        float cLo;
        float cHi;
        int iLo;
        int iHi;
    };
    using Breakpoints = std::vector<Breakpoint>;
    using BreakpointsMap = std::map<Pollutant, Breakpoints>;

    using Concentration = std::pair<Pollutant, float>;
    using Concentrations = std::vector<Concentration>;

    AQI(Algorithm algo=Algorithm::EPA);

    int GetIntermediateIndex(const Concentration& con);
    int GetIndex(const Concentrations& con);
    float GetMaxConcentration(Pollutant p);
    static float GetPrecision(Pollutant p);
    static const char* GetUnits(Pollutant p);

private:
    const Breakpoints* getBreakpoints(Pollutant p);
    void initAlgos();

    Algorithm _algo;
    std::map<Algorithm, BreakpointsMap> _algos;
};
