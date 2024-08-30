/***************************************************************************************/
/*  Filename: SOP_GSplat.h                                                             */
/*  Description: Surface Operator to create GSplat primitives                          */
/*                                                                                     */
/*  Copyright (C) 2024 Ruben Diaz                                                      */
/*                                                                                     */
/*  License: AGPL-3.0-or-later                                                         */
/*           https://github.com/rubendhz/houdini-gsplat-renderer/blob/develop/LICENSE  */
/***************************************************************************************/


#ifndef __SOP_GSPLAT__
#define __SOP_GSPLAT__


#include <SOP/SOP_Node.h>


class SOP_Gsplat : public SOP_Node
{
public:
    static OP_Node *myConstructor(OP_Network*, const char *, OP_Operator *);
    static PRM_Template myTemplateList[];

protected:
    SOP_Gsplat(OP_Network *net, const char *name, OP_Operator *op);
    ~SOP_Gsplat() override;

    OP_ERROR cookMySop(OP_Context &context) override;
};


#endif // __SOP_GSPLAT__
