#pragma once
// Persian (Solar Hijri / Jalali) calendar helpers for the Iranian occasions (display_occasion.cpp).
// Pure arithmetic, no Arduino / ESP32 dependency, so it can be unit-tested on a PC.
//
// The conversion uses the 33-year arithmetic rule (leap years: year mod 33 = 1, 5, 9, 13, 17, 22, 26, 30).
// It matches the official Iranian calendar for the years the clock will realistically see (about 1304 .. 1472,
// i.e. 1925 .. 2093); after that the official calendar (astronomical) and the rule may differ by a day.
namespace jalali
{
    inline bool isLeap(int jy)
    {
        int r = ((jy % 33) + 33) % 33;
        return r == 1 || r == 5 || r == 9 || r == 13 || r == 17 || r == 22 || r == 26 || r == 30;
    }

    // Days in month jm (1 = Farvardin .. 12 = Esfand) of year jy.
    inline int monthLength(int jy, int jm)
    {
        if (jm <= 6)
            return 31;
        if (jm <= 11)
            return 30;
        return isLeap(jy) ? 30 : 29;
    }

    // Gregorian date -> Persian date (jy, jm 1..12, jd 1..31).
    inline void fromGregorian(int gy, int gm, int gd, int &jy, int &jm, int &jd)
    {
        static const int gMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        static const int jMonth[12] = {31, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 29};
        gy -= 1600;
        gm -= 1;
        gd -= 1;
        long gDayNo = 365L * gy + (gy + 3) / 4 - (gy + 99) / 100 + (gy + 399) / 400;
        for (int i = 0; i < gm; i++)
            gDayNo += gMonth[i];
        if (gm > 1 && ((gy % 4 == 0 && gy % 100 != 0) || (gy % 400 == 0)))
            gDayNo++; // Gregorian leap day already passed
        gDayNo += gd;

        long jDayNo = gDayNo - 79;
        long jNp = jDayNo / 12053; // 33-year cycles
        jDayNo %= 12053;
        int y = 979 + 33 * (int)jNp + 4 * (int)(jDayNo / 1461);
        jDayNo %= 1461;
        if (jDayNo >= 366)
        {
            y += (int)((jDayNo - 1) / 365);
            jDayNo = (jDayNo - 1) % 365;
        }
        int i = 0;
        for (; i < 11 && jDayNo >= jMonth[i]; i++)
            jDayNo -= jMonth[i];
        jy = y;
        jm = i + 1;
        jd = (int)jDayNo + 1;
    }

    // Day of Esfand (1..29/30) of the LAST Wednesday of the Persian year. Pass any day jd of Esfand of year jy
    // and its weekday wday (0 = Sunday .. 6 = Saturday). Chaharshanbe Suri is the Tuesday evening before it.
    inline int lastWednesdayOfEsfand(int jy, int jd, int wday)
    {
        int len = monthLength(jy, 12);
        int wdEnd = (wday + (len - jd)) % 7; // weekday of the last day of the year
        return len - ((wdEnd + 4) % 7);      // + 4 == - 3 (Wednesday) mod 7
    }
} // namespace jalali