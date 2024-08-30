#ifndef __GSPLAT_PLUGIN_VERSION__
#define __GSPLAT_PLUGIN_VERSION__

#define GSPLAT_PLUGIN_VERSION "1.0.0"

#include <iostream>

static bool myDidPrintVersion = false;
void printPluginVersionOnce() 
{
    if (!myDidPrintVersion) 
    {
        myDidPrintVersion = true;
        std::cout << "GSplat Plugin version: " << GSPLAT_PLUGIN_VERSION << std::endl;
    }
}

#endif // __GSPLAT_PLUGIN_VERSION__