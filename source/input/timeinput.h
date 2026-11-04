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

#ifndef X265_TIMEINPUT_H
#define X265_TIMEINPUT_H

#include "common.h"
#include <cstdio>
#include <cstdint>

namespace X265_NS {
// x265 private namespace

/* Source of per-frame durations for VFR encoding, in VUI clock ticks
 * (timescale = time_scale / num_units_in_tick). Use open() to get the reader
 * matching the file; subclasses implement one file format each. */
class TimeInput
{
public:
    TimeInput(const char* fname, uint32_t vuiTimescaleNum, uint32_t vuiTimescaleDenom);
    virtual ~TimeInput();

    /* returns the reader for the format of fname. Check isFail() on the result. */
    static TimeInput* open(const char* fname, uint32_t vuiTimescaleNum, uint32_t vuiTimescaleDenom);

    virtual const char *getName() const = 0;
    bool isFail() const           { return !ifs; }

    /* returns the duration of frame sourcePoc in clock ticks. POC must be monotonic. */
    uint32_t getFrameTicksDuration(const uint32_t sourcePoc) { return readNext(sourcePoc); }

protected:
    /* parse the file header, return the format version or <= 0 on failure */
    virtual int readHeader() = 0;
    /* consume the file up to sourcePoc and return the frame duration in clock ticks */
    virtual uint32_t readNext(uint32_t sourcePoc) = 0;

    static void reduceFraction(uint64_t& n, uint64_t& d);

    FILE *ifs;
    int version;

    /* bitstream timescale (VUI time_scale / num_units_in_tick) */
    uint64_t vuiTimescaleNum;
    uint64_t vuiTimescaleDenom;
};

/* Reads an x265 timescale file: optional "# x265 timescale format v1" header, then
 * one "<poc> <fps>" entry per line, where <fps> is an integer or a fraction "num/denom".
 * A rate applies from its POC onwards until the next entry. Also tracks the current
 * rounding error w.r.t. the VUI timebase */
class TimescaleInput : public TimeInput
{
public:
    TimescaleInput(const char* fname, uint32_t vuiTimescaleNum, uint32_t vuiTimescaleDenom);

    const char *getName() const   { return "timescale"; }

protected:
    int readHeader();
    uint32_t readNext(uint32_t sourcePoc);

private:
    void readNextRate(uint32_t sourcePoc);

    static int64_t rescaleError(int64_t err, int64_t oldDen, int64_t newDen);

    /* rounding residual, in units of 1/rateRatioDenom clock ticks */
    int64_t errorInTicks;

    /* latest frame rate parsed in the timescale file, for reference */
    uint64_t rateNum;
    uint64_t rateDenom;

    /* cached frame duration in ticks: rateRatioNum / rateRatioDenom */
    int64_t rateRatioNum;
    int64_t rateRatioDenom;
};

/* Reads a MKV timestamp v2 file. It shall have a "# timestamp format v2" header, then
 * one "<pts>" entry per line, <pts> in millisecond, either an integer or a double.
 * if the file is consumed while there are still frames, the last valid duration is carried. */
class TimestampInput : public TimeInput
{
public:
    TimestampInput(const char* fname, uint32_t vuiTimescaleNum, uint32_t vuiTimescaleDenom);

    const char *getName() const   { return "timestamp"; }

protected:
    int readHeader();
    uint32_t readNext(uint32_t sourcePoc);

private:
    double readNextTimestamp();

    double errorInSecs;
    double timeShiftSecs;

    double lastTimestamp;
    double lastDuration;
};

}

#endif // ifndef X265_TIMEINPUT_H
