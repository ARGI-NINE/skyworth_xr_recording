//
// Created by Richard on 2021-09-14.
//

#include <string>
#include <cstdio>
#include <errno.h>
#include <android/asset_manager.h>
#include "TextureUtils.h"
#include "xr_logger.h"

bool loadTexture(const char* filename){
//header for testing if it is a png
    unsigned char *buffer;
    //open file as binary
    FILE *pixmap = fopen(filename, "wb+");
    if (!pixmap) {
        XR_ERROR("open file error path %s,error:%d,%s",filename,errno ,strerror(errno));
        return false;
    }
    fseek(pixmap,0, SEEK_END);
    int length = ftell(pixmap);//读取图片的大小长度
    XR_DEBUG("pixmap:%d",length);
    buffer = (unsigned char*)malloc(length*sizeof(unsigned char));
    fseek(pixmap, 0, SEEK_SET);//把光标设置到文件的开头
    while(0 != fread(buffer,sizeof(unsigned char),length,pixmap)){
    }
    fclose(pixmap);
    return true;
}


