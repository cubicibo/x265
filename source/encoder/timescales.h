/*****************************************************************************
 * Copyright (C) 2026 MulticoreWare, Inc
 *
 * Authors: cubicibo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at license @ x265.com.
 *****************************************************************************/

#ifndef X265_TIMESCALES_H
#define X265_TIMESCALES_H

#include "common.h"
#include <cstdio>
#include <cstdint>

namespace X265_NS {
// x265 private namespace

/* Reads a timescale file: one "<poc> <fps>" entry per line, where <fps> is
 * an integer or a fraction "num/denom". A rate applies from its POC onwards
 * until the next entry. Also tracks the current error w.r.t. the VUI scale */
class TimescaleInput
{
public:
    TimescaleInput(const char* fname, uint32_t vuiTimescaleNum, uint32_t vuiTimescaleDenom);
    ~TimescaleInput();

    const char *getName() const   { return "timescale"; }
    bool isFail() const           { return !ifs; }

    /* returns the duration of frame sourcePoc in clock ticks.
     * POC should be monotonic in a session. */
    uint32_t getFrameTicksDuration(const uint32_t sourcePoc);

protected:
    FILE *ifs;

private:
    /* Find latest applicable fps given sourcePoc */
    void readNextRate(uint32_t sourcePoc);

    static void reduceFraction(uint64_t& n, uint64_t& d);

    static int64_t rescaleError(int64_t err, int64_t oldDen, int64_t newDen);

    /* bitstream timebase (VUI time_scale / num_units_in_tick) */
    uint64_t vuiTimescaleNum;
    uint64_t vuiTimescaleDenom;

    /* rounding residual, in units of 1/rateRatioDenom clock ticks */
    int64_t errorInTicks;

    /* latest frame rate parsed in the timescale file, for reference */
    uint64_t rateNum;
    uint64_t rateDenom;

    /* cached frame duration in ticks: rateRatioNum / rateRatioDenom */
    int64_t rateRatioNum;
    int64_t rateRatioDenom;
};
}

#endif // ifndef X265_TIMESCALES_H
