#pragma once
#include "CardType.h"
#include <vector>
#include <utility>
#include <cstdlib>

namespace RangeShape
{
    // ’†S(cc,cr) ‚©‚çŒ©‚Ä (col,row) ‚ª”ÍˆÍ“à‚©
    inline bool Contains(int cc, int cr, int col, int row,
        RangeType type, int range, int minRange = 0, int aimDx = 0, int aimDy = 0)
    {
        int dc = abs(col - cc), dr = abs(row - cr);
        switch (type)
        {
        case RangeType::None:     return false;
        case RangeType::Adjacent: return (dc + dr) == 1;
        case RangeType::Cross:
        {
            int lo = minRange < 1 ? 1 : minRange;
            if (dc == 0 && dr >= lo && dr <= range) return true;
            if (dr == 0 && dc >= lo && dc <= range) return true;
            return false;
        }
        case RangeType::Area:
        {
            int c = (dc > dr ? dc : dr);
            return c > 0 && c >= minRange && c <= range;
        }
        case RangeType::Diamond:
        {
            int m = dc + dr;
            return m > 0 && m >= minRange && m <= range;
        }
        case RangeType::Diagonal:
            return dc == dr && dc >= (minRange < 1 ? 1 : minRange) && dc <= range;
        case RangeType::DiagonalCross:
            return ((dc == 0 || dr == 0) || (dc == dr))
                && (dc + dr) > 0 && (dc > dr ? dc : dr) <= range;
        case RangeType::Cone:
        {
            int oc = col - cc, orr = row - cr;
            int along = oc * aimDx + orr * aimDy;
            int perp = abs(oc * aimDy - orr * aimDx);
            return along >= 1 && along <= range && perp <= along - 1;
        }
        default: return false;
        }
    }
}