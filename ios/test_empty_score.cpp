#include "FileSystem.hpp"
#include "ResultScreen.hpp"
#include "Supervisor.hpp"
#include "pbg4/Lzss.hpp"

#include <cstdarg>
#include <cstdlib>
#include <cstring>

u32 g_LastFileSize = 0;

u8 *FileSystem::OpenFile(const char *, i32)
{
    return NULL;
}

void Supervisor::DebugPrint(const char *, ...)
{
}

u8 *Lzss::Decompress(u8 *, i32, u8 *dst, u32)
{
    return dst;
}

int main()
{
    ScoreDat *score = ResultScreen::OpenScore("missing-score.dat");
    if (!score || score->decodedData || !score->scores ||
        score->raw.dataOffset != sizeof(ScoreDatRaw) ||
        score->raw.fileLength != sizeof(ScoreDatRaw))
    {
        return 1;
    }

    Plst plst = {};
    Catk catk[141] = {};
    Clrd clrd[6] = {};
    Pscr pscr[6][6][4] = {};
    Lsnm lsnm = {};
    u8 retries = 99;

    if (ResultScreen::ParsePlst(score, &plst) != ZUN_SUCCESS ||
        ResultScreen::ParseCatk(score, catk) != ZUN_SUCCESS ||
        ResultScreen::ParseClrd(score, clrd) != ZUN_SUCCESS ||
        ResultScreen::ParsePscr(score, &pscr[0][0][0]) != ZUN_SUCCESS ||
        ResultScreen::ParseLsnm(score, &lsnm) != 0 ||
        ResultScreen::GetHighScore(score, NULL, 0, 0, &retries) != 100000 || retries != 0)
    {
        ResultScreen::ReleaseScoreDat(score);
        return 2;
    }

    ResultScreen::ReleaseScoreDat(score);

    score = new ScoreDat;
    score->raw.dataOffset = sizeof(ScoreDatRaw);
    score->raw.fileLength = sizeof(ScoreDatRaw) + sizeof(Th7k);
    score->decodedData = (u8 *)std::calloc(1, score->raw.fileLength);
    score->scores = new ScoreListNode;
    Th7k *badChunk = (Th7k *)(score->decodedData + sizeof(ScoreDatRaw));
    badChunk->magic = PLST_MAGIC;
    badChunk->version = 1;
    badChunk->th7kLen = 0;

    retries = 99;
    if (ResultScreen::ParsePlst(score, &plst) != ZUN_SUCCESS ||
        ResultScreen::ParseCatk(score, catk) != ZUN_SUCCESS ||
        ResultScreen::ParseClrd(score, clrd) != ZUN_SUCCESS ||
        ResultScreen::ParsePscr(score, &pscr[0][0][0]) != ZUN_SUCCESS ||
        ResultScreen::ParseLsnm(score, &lsnm) != 0 ||
        ResultScreen::GetHighScore(score, NULL, 0, 0, &retries) != 100000 || retries != 0)
    {
        ResultScreen::ReleaseScoreDat(score);
        return 3;
    }
    ResultScreen::ReleaseScoreDat(score);
    return 0;
}
