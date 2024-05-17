#include "SOP_GSplat.h"
#include "GEO_GSplat.h"

#include <GU/GU_Detail.h>
#include <OP/OP_Operator.h>
#include <OP/OP_OperatorTable.h>
#include <CH/CH_LocalVariable.h>
#include <PRM/PRM_Include.h>
#include <UT/UT_DSOVersion.h>
#include <UT/UT_Interrupt.h>
#include <UT/UT_Vector3.h>
#include <SYS/SYS_Types.h>
#include <OP/OP_AutoLockInputs.h>

#include <limits.h>
#include <stddef.h>


///
/// newSopOperator is the hook that Houdini grabs from this dll
/// and invokes to register the SOP.  In this case we add ourselves
/// to the specified operator table.
///
void
newSopOperator(OP_OperatorTable *table)
{
    table->addOperator(new OP_Operator(
        "GSplatSOP",                // Internal name
        "GSplatSOP",                // UI name
        SOP_Gsplat::myConstructor,  // How to build the SOP
        SOP_Gsplat::myTemplateList, // My parameters
        1,                          // Min # of sources
        1,                          // Max # of sources
        nullptr,                    // Local variables  
        0));                        // Flags it's not as generator (i.e. OP_FLAG_GENERATOR)    
}

PRM_Template
SOP_Gsplat::myTemplateList[] = {
    PRM_Template()
};


OP_Node *
SOP_Gsplat::myConstructor(OP_Network *net, const char *name, OP_Operator *op)
{
    return new SOP_Gsplat(net, name, op);
}

SOP_Gsplat::SOP_Gsplat(OP_Network *net, const char *name, OP_Operator *op)
    : SOP_Node(net, name, op)
{
    // This indicates that this SOP manually manages its data IDs,
    // so that Houdini can identify what attributes may have changed,
    // e.g. to reduce work for the viewport, or other SOPs that
    // check whether data IDs have changed.
    // By default, (i.e. if this line weren't here), all data IDs
    // would be bumped after the SOP cook, to indicate that
    // everything might have changed.
    // If some data IDs don't get bumped properly, the viewport
    // may not update, or SOPs that check data IDs
    // may not cook correctly, so be *very* careful!
    mySopFlags.setManagesDataIDs(true);
}

SOP_Gsplat::~SOP_Gsplat() {}

OP_ERROR SOP_Gsplat::cookMySop(OP_Context &context) 
{    
    OP_AutoLockInputs inputs(this);
    if (inputs.lock(context) >= UT_ERROR_ABORT)
        return error();

    const GU_Detail *inputGdp = inputGeo(0, context);

    if (!inputGdp) return error();

    // Clear existing geometry and prepare for new geometry in the SOP's output
    gdp->clearAndDestroy();

    // Merge points from the input geometry into this SOP's geometry.
    // This will include point attributes by default.
    gdp->mergePoints(*inputGdp);

    // Create a new GEO_PrimGsplat primitive in the output geometry
    //GEO_PrimGsplat *gsplatPrim = GEO_PrimGsplat::build(gdp, false); 
    GEO_PrimGsplat::build(gdp);

    gdp->bumpAllDataIds();

    return error();
}


