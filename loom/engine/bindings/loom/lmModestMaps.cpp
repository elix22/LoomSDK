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

#include "loom/common/core/log.h"
#include "loom/common/core/assert.h"
#include "loom/script/loomscript.h"
#include "loom/script/runtime/lsRuntime.h"

#include "loom/graphics/gfxMath.h"
#include "loom/engine/loom2d/l2dDisplayObject.h"
#include "loom/engine/loom2d/l2dMatrix.h"


using namespace LS;
using namespace Loom2D;



/// Script bindings to the native ModestMaps API.
///
/// See ModestMaps.ls for documentation on this API.
class ModestMaps
{
public:
    static float lastCoordinateX;
    static float lastCoordinateY;
    static int parentLoadCol;
    static int parentLoadRow;
    static int parentLoadZoom;


    static const char *tileKey(int col, int row, int zoom)
    {
        char key[256];
        sprintf(key, "%c:%i:%i", (char)(((int)'a')+zoom), col, row);
        return (const char *)key;
    }
 
    static const char *prepParentLoad(int col, int row, int zoom, int parentZoom)
    {
        //NOTE_TEC: zoomDiff should always be +ve
        int zoomDiff = zoom - parentZoom;
        if(zoomDiff <= 0)
        {
            parentLoadCol = col;
            parentLoadRow = row;
            parentLoadZoom = zoom;
        }
        else
        {
            float invScaleFactor = 1.0f / (float)(1 << zoomDiff);
            parentLoadCol = floor((float)col * invScaleFactor); 
            parentLoadRow = floor((float)row * invScaleFactor);
            parentLoadZoom = parentZoom;
        }
        return tileKey(parentLoadCol, parentLoadRow, parentLoadZoom);
    }

    static void setLastCoordinate(float col,
                                    float row,
                                    float zoom,
                                    float zoomLevel,
                                    float invTileWidth,
                                    Matrix *worldMatrix,
                                    DisplayObject *context,
                                    DisplayObject *object)
    {
        // this is basically the same as coord.zoomTo, but doesn't make a new Coordinate:
        float zoomFactor = pow(2, zoomLevel - zoom) * invTileWidth;
        float zoomedColumn = col * zoomFactor;
        float zoomedRow = row * zoomFactor;
                    
        worldMatrix->transformCoordInternal(zoomedColumn, zoomedRow, &lastCoordinateX, &lastCoordinateY);

        //transform into correct space if necessary
        if ((context != NULL) && (context != object))
        {
            localToGlobal((DisplayObject *)(object->parent), &lastCoordinateX, &lastCoordinateY);
            globalToLocal(context, &lastCoordinateX, &lastCoordinateY);
        }
    } 

 
    static const char *getMSProviderZoomString(float col, float row, int zoom)
    {
        // we don't wrap rows here because the map/grid should be enforcing outerLimits :)
        float zoomExp = pow(2, zoom);
        float wrappedColumn = fmod(col, zoomExp);
        while (wrappedColumn < 0)
        {
            wrappedColumn += zoomExp;
        }
        col = wrappedColumn;
        
        // convert row + col to zoom string
        // padded with zeroes so we end up with zoom digits after slicing:
        convertToBinary(row, _rowBinaryString);
        convertToBinary(col, _colBinaryString);

        // generate zoom string
        int rowOffset = strlen(_rowBinaryString) - zoom;
        int colOffset = strlen(_colBinaryString) - zoom;
        char *zoomString = (char *)malloc(sizeof(char) * (zoom + 1));
        for(int i = 0; i < zoom; i++) 
        {
            //proces the row and col bits to build up the zoom string; values of 0,1,2,3
            char value = _colBinaryString[i + colOffset];
            if(_rowBinaryString[i + rowOffset] == '1')
            {
                value = (value == '1') ? '3' : '2';
            }
            zoomString[i] = value;
        }
        zoomString[zoom] = '\0';
        return (const char *)zoomString; 
    }



private:
    static char _rowBinaryString[33];
    static char _colBinaryString[33];


    static void localToGlobal(DisplayObject *obj, float *x, float *y)
    {
        //find the base of the object to start
        DisplayObject *base = obj;
        while (base->parent) { base = (DisplayObject *)base->parent; }

        //get the matrix and transform!
        Matrix mtx;
        obj->getTargetTransformationMatrix(base, &mtx);
        mtx.transformCoordInternal(*x, *y, x, y);
    }


    static void globalToLocal(DisplayObject *obj, float *x, float *y)
    {
        //find the base of the object to start
        DisplayObject *base = obj;
        while (base->parent) { base = (DisplayObject *)base->parent; }

        //get the matrix and transform!
        Matrix mtx;
        obj->getTargetTransformationMatrix(base, &mtx);
        mtx.invert();
        mtx.transformCoordInternal(*x, *y, x, y);
    }

    /** 
     * @return 32 digit binary representation of numberToConvert
     *  
     * NOTE: Due to Loom Script not having unsigned int values, this will 
     * only work correctly for number with values up to up to 2147483647
     */
    static void convertToBinary(int numberToConvert, char *binString) 
    {
        bool negative = false;       
        if(numberToConvert < 0)
        {
            //we want to wrap -ve values around as if we were casting to uint, so 2s comp FTW!
            numberToConvert += (1 << 30);
            negative = true;
        }

        //convert to a binary string
        binString[32] = '\0';
        int numBits = 32;
        int remainder;
        while (numberToConvert > 0)
        {
            remainder = (int)(numberToConvert % 2);
            numberToConvert = (int)(numberToConvert / 2);
            binString[--numBits] = (remainder == 0) ? '0' : '1';
        }

        //add preceeding digits if necessary
        if (numBits > 0) 
        {
            memset(binString, (negative) ? '1' : '0', numBits);
        }
    }    
};

float ModestMaps::lastCoordinateX = 0.0f;
float ModestMaps::lastCoordinateY = 0.0f;
int ModestMaps::parentLoadCol = 0;
int ModestMaps::parentLoadRow = 0;
int ModestMaps::parentLoadZoom = 0;
char ModestMaps::_rowBinaryString[33];
char ModestMaps::_colBinaryString[33];




static int registerLoomModestMaps(lua_State *L)
{
    ///set up lua bindings
    beginPackage(L, "loom.modestmaps")

        .beginClass<ModestMaps>("ModestMaps")

            .addStaticVar("LastCoordinateX", &ModestMaps::lastCoordinateX)
            .addStaticVar("LastCoordinateY", &ModestMaps::lastCoordinateY)
            .addStaticVar("ParentLoadCol", &ModestMaps::parentLoadCol)
            .addStaticVar("ParentLoadRow", &ModestMaps::parentLoadRow)
            .addStaticVar("ParentLoadZoom", &ModestMaps::parentLoadZoom)

            .addStaticMethod("tileKey", &ModestMaps::tileKey)
            .addStaticMethod("prepParentLoad", &ModestMaps::prepParentLoad)
            .addStaticMethod("setLastCoordinate", &ModestMaps::setLastCoordinate)
            .addStaticMethod("getMSProviderZoomString", &ModestMaps::getMSProviderZoomString)

        .endClass()

    .endPackage();

    return 0;
}


void installLoomModestMaps()
{
    LOOM_DECLARE_NATIVETYPE(ModestMaps, registerLoomModestMaps);
}
