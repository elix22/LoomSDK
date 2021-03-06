/*
 * ===========================================================================
 * Loom SDK
 * Copyright 2011, 2012, 2013
 * The Game Engine Company, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ===========================================================================
 */

#include <string.h>
#include "wavloader.h"
#include "loom/common/core/log.h"
#include "loom/common/utils/utEndian.h"

extern loom_logGroup_t gSoundAssetGroup;

typedef struct
{
    char chunkId[4];
    uint32_t chunkDataSize;
} chunk_header;

typedef struct
{
    chunk_header header;
    char riffType[4];
} riff_header;

typedef struct
{
    uint16_t compressionType;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t avgBytesPerSecond;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
} fmt_chunk;

bool load_wav(const uint8_t* inData,
              const int32_t inDataLen,
              uint8_t* outData,
              wav_info* outInfo)
{
    if (inData == NULL)
    {
        lmLogError(gSoundAssetGroup, "No input data passed to wav loader");
        return false;
    }

    const uint8_t* cursor = inData;

    riff_header riff = *((riff_header*)cursor);
    if (riff.header.chunkId[0] != 'R' ||
        riff.header.chunkId[1] != 'I' ||
        riff.header.chunkId[2] != 'F' ||
        riff.header.chunkId[3] != 'F')
    {
        lmLogError(gSoundAssetGroup, "Bad wav file format");
        return false;
    }

    if (riff.riffType[0] != 'W' ||
        riff.riffType[1] != 'A' ||
        riff.riffType[2] != 'V' ||
        riff.riffType[3] != 'E')
    {
        lmLogError(gSoundAssetGroup, "Bad wav file format");
        return false;
    }

    riff.header.chunkDataSize = convertLEndianToHost(riff.header.chunkDataSize);

    if (inDataLen < static_cast<int32_t>(sizeof(chunk_header) + riff.header.chunkDataSize))
    {
        lmLogError(gSoundAssetGroup, "Not enough data in wav buffer");
        return false;
    }

    cursor += sizeof(riff_header);

    while (cursor < inData + inDataLen && cursor < inData + riff.header.chunkDataSize)
    {
        chunk_header curChunkHeader = *((chunk_header*)cursor);
        curChunkHeader.chunkDataSize = convertLEndianToHost(curChunkHeader.chunkDataSize);

        if (curChunkHeader.chunkId[0] == 'f' &&
            curChunkHeader.chunkId[1] == 'm' &&
            curChunkHeader.chunkId[2] == 't' &&
            curChunkHeader.chunkId[3] == ' ')
        {
            fmt_chunk fmt = *((fmt_chunk*)(cursor + sizeof(chunk_header)));
            fmt.compressionType = convertLEndianToHost(fmt.compressionType);

            if (fmt.compressionType != 0x01)
            {
                // This loader only supports simple PCM (8-bit or 16-bit, which should be the most common)
                lmLogError(gSoundAssetGroup, "Unsupported wav format: 0x%2x. Recommend 8 or 16-bit PCM",
                           fmt.compressionType);
                return false;
            }

            if (outInfo)
            {
                outInfo->numChannels = convertLEndianToHost(fmt.numChannels);
                outInfo->samplesPerSecond = convertLEndianToHost(fmt.sampleRate);
                outInfo->sampleSize = convertLEndianToHost(fmt.bitsPerSample);
            }
        }
        else if (curChunkHeader.chunkId[0] == 'd' &&
                 curChunkHeader.chunkId[1] == 'a' &&
                 curChunkHeader.chunkId[2] == 't' &&
                 curChunkHeader.chunkId[3] == 'a')
        {
            if (outInfo)
            {
                outInfo->sampleDataSize = curChunkHeader.chunkDataSize;
            }

            if (outData != NULL)
            {
                memcpy(outData, cursor + sizeof(chunk_header), curChunkHeader.chunkDataSize);
            }
        }

        cursor += sizeof(chunk_header) + curChunkHeader.chunkDataSize;
    }

    return true;
}