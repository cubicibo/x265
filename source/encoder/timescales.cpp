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

#include "common.h"
#include "timescales.h"

#include <iostream>
#include <cmath>
#include <cstring>
#include <climits>
#include <cstdlib>
#include <cctype>

/* same limit as MAX_FRAME_RATE in input/input.h, which libx265 cannot include */
#define TIMESCALE_MAX_FRAME_RATE 300

/* prevent excessive numerators and denominators */
#define MAX_NUM_DEN_VAL (TIMESCALE_MAX_FRAME_RATE * 1000)

#if MAX_NUM_DEN_VAL > (1u << 20)
# error "risk of overflow in error scaling"
#endif

using namespace X265_NS;
using namespace std;

TimescaleInput::TimescaleInput(const char* fname, uint32_t timescaleNum, uint32_t timescaleDenom)
{
    this->ifs = NULL;
    this->vuiTimescaleNum = timescaleNum;
    this->vuiTimescaleDenom = timescaleDenom;
    this->errorInTicks = 0;

    if (!timescaleNum || !timescaleDenom)
    {
        x265_log(NULL, X265_LOG_ERROR, "timescale: invalid bitstream timescale %u/%u\n",
                 timescaleNum, timescaleDenom);
        return;
    }
    reduceFraction(this->vuiTimescaleNum, this->vuiTimescaleDenom);

    /* until the file says otherwise, frames run at the container timebase: 1 tick each */
    this->rateNum = this->vuiTimescaleNum;
    this->rateDenom = this->vuiTimescaleDenom;
    this->rateRatioNum = 1;
    this->rateRatioDenom = 1;

    if (!strcmp(fname, "-"))
    {
        x265_log(NULL, X265_LOG_ERROR, "timescale: cannot use pipe input\n");
        return;
    }
    this->ifs = x265_fopen(fname, "rb");
}

TimescaleInput::~TimescaleInput()
{
    if (ifs)
        fclose(ifs);
}

void TimescaleInput::readNextRate(uint32_t sourcePoc)
{
    if (!ifs)
        return;

    char buff[384];
    bool bChanged = false;
    uint64_t newNum = rateNum, newDenom = rateDenom;

    while (1)
    {
        long filePos = ftell(ifs);
        if (!fgets(buff, sizeof(buff), ifs))
            break;

        int c = 0;
        if (!strchr(buff, '\n') && !feof(ifs))
        {
            while ((c = fgetc(ifs)) != '\0')
            {
                if (c == '\n' || c == EOF)
                    break;
            }
        }

        uint32_t poc;
        uint32_t fpsNum, fpsDen = 1;

        /* blank lines and '#' comments are skipped silently */
        const char *line = buff;
        while (isspace((unsigned char)*line))
            line++;
        if (!*line || *line == '#')
            continue;

        /* "poc num" or "poc num/den" */
        int n = 0;
        if (sscanf(line, "%u %u%n", &poc, &fpsNum, &n) < 2)
        {
            x265_log(NULL, X265_LOG_WARNING, "timescale: cannot parse line: %s", buff);
            continue;
        }
        const char *rest = line + n;
        if (*rest == '/')
        {
            n = 0;
            if (sscanf(rest + 1, "%u%n", &fpsDen, &n) < 1)
                fpsDen = 0; /* rejected below */
            rest += n;
        }

        if (poc > sourcePoc)
        {
            /* entry for a future frame: rewind so it is read again later */
            fseek(ifs, filePos, SEEK_SET);
            break;
        }

        if (!fpsNum || !fpsDen || fpsNum > MAX_NUM_DEN_VAL || fpsDen > MAX_NUM_DEN_VAL)
        {
            x265_log(NULL, X265_LOG_WARNING, "timescale: invalid rate %u/%u at poc %u, ignored\n", fpsNum, fpsDen, poc);
            continue;
        }

        /* guard against zero-tick (current fps cannot exceed timebase) */
        if ((uint64_t)fpsNum * vuiTimescaleDenom > vuiTimescaleNum * (uint64_t)fpsDen)
        {
            x265_log(NULL, X265_LOG_WARNING, "timescale: rate %u/%u at poc %u exceeds bitstream %llu/%llu, clamped\n",
                     fpsNum, fpsDen, poc, (unsigned long long)vuiTimescaleNum, (unsigned long long)vuiTimescaleDenom);
            newNum = vuiTimescaleNum;
            newDenom = vuiTimescaleDenom;
        }
        else
        {
            newNum = fpsNum;
            newDenom = fpsDen;
        }
        /* it's tempting to break but if there's a gap in sourcePoc from a prior one then we need to continue */
        bChanged = true;
    }

    if (bChanged)
    {
        reduceFraction(newNum, newDenom);
        rateNum = newNum;
        rateDenom = newDenom;

        uint64_t a = vuiTimescaleNum * rateDenom;
        uint64_t b = vuiTimescaleDenom * rateNum;
        reduceFraction(a, b);

        errorInTicks = rescaleError(errorInTicks, rateRatioDenom, (int64_t)b);
        rateRatioNum = (int64_t)a;
        rateRatioDenom = (int64_t)b;
    }
}

int64_t TimescaleInput::rescaleError(int64_t err, int64_t oldDen, int64_t newDen)
{
    if (!err || oldDen == newDen)
        return err;

    /* round away from zero */
    int64_t p = err * newDen, r;
    r = (p >= 0 ? (p + oldDen / 2) : (p - oldDen / 2)) / oldDen;

    /* clamp to +/- half a tick away to avoid zero duration */
    return x265_clip3<int64_t>(-(newDen >> 1), newDen - (newDen >> 1) - 1, r);
}

uint32_t TimescaleInput::getFrameTicksDuration(const uint32_t sourcePoc)
{
    this->readNextRate(sourcePoc);

    int64_t frameDuration = (rateRatioNum + errorInTicks + (rateRatioDenom >> 1)) / rateRatioDenom;
    errorInTicks += rateRatioNum - frameDuration * rateRatioDenom;

    /* if frame duration exceeds 1<<32 ticks then something is very wrong: caller should handle this */
    return (uint32_t)X265_MIN(UINT_MAX, frameDuration);
}

void TimescaleInput::reduceFraction(uint64_t& n, uint64_t& d)
{
    if (!d || !n)
        return;

    uint64_t a = n, b = d;
    while (b > 0)
    {
        uint64_t c = b;
        b = a % b;
        a = c;
    }

    n /= a;
    d /= a;
}
