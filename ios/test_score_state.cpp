#include "ResultScreen.hpp"

#include <cstring>

int main()
{
    const ScoreDatRaw zero = {};
    for (int i = 0; i < 1000000; ++i)
    {
        ScoreDat score;
        if (score.scores != NULL || score.decodedData != NULL ||
            std::memcmp(&score.raw, &zero, sizeof(zero)) != 0)
        {
            return 1;
        }
    }
    return 0;
}
