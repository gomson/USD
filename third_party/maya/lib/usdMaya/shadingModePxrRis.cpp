//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pxr/pxr.h"
#include "usdMaya/util.h"
#include "usdMaya/writeUtil.h"

#include "usdMaya/shadingModeRegistry.h"
#include "usdMaya/shadingModeExporter.h"
#include "usdMaya/shadingModeExporterContext.h"

#include "pxr/usd/usd/prim.h"

#include "pxr/usd/usdShade/connectableAPI.h"
#include "pxr/usd/usdShade/material.h"

#include "pxr/usd/usdRi/materialAPI.h"
#include "pxr/usd/usdRi/risBxdf.h"
#include "pxr/usd/usdRi/risObject.h"
#include "pxr/usd/usdRi/risPattern.h"

#include "pxr/usd/usdGeom/gprim.h"

#include <maya/MFnDependencyNode.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>

#include <boost/assign/list_of.hpp>

// Defines the RenderMan for Maya mapping between Pxr objects and Maya internal nodes
#include "usdMaya/shadingModePxrRis_rfm_map.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {
class PxrRisShadingModeExporter : public PxrUsdMayaShadingModeExporter {
public:
    PxrRisShadingModeExporter() {}
private:
    std::string
    _GetShaderTypeName(const MFnDependencyNode& depNode)
    {
        std::string risShaderType(depNode.typeName().asChar());
        // Now look into the RIS TABLE if the typeName doesn't starts with Pxr
        if (!TfStringStartsWith(risShaderType, "Pxr")) {
            for (size_t i=0;i<_RFM_RISNODE_TABLE.size();i++) {
                if (_RFM_RISNODE_TABLE[i].first==risShaderType) {
                    risShaderType=_RFM_RISNODE_TABLE[i].second;
                    break;
                }
            }
        }
        return risShaderType;
    }

    UsdPrim
    _ExportShadingNode(const UsdPrim& materialPrim,
                       const MFnDependencyNode& depNode,
                       const PxrUsdMayaShadingModeExportContext& context,
                       SdfPathSet *processedPaths,
                       bool isFirstNode
    )
    {
        UsdStagePtr stage = materialPrim.GetStage();

        // XXX: would be nice to write out the current display color as
        // well.  currently, when we re-import, we don't get the display color so
        // it shows up as black.

        TfToken shaderPrimName(PxrUsdMayaUtil::SanitizeName(depNode.name().asChar()));
        SdfPath shaderPath = materialPrim.GetPath().AppendChild(shaderPrimName);
        if (processedPaths->count(shaderPath) == 1){
            return stage->GetPrimAtPath(shaderPath);
        }

        processedPaths->insert(shaderPath);

        // Determie the risShaderType that will correspont to the USD FilePath
        std::string risShaderType(_GetShaderTypeName(depNode));

        if (!TfStringStartsWith(risShaderType, "Pxr")) {
            MGlobal::displayError(TfStringPrintf(
                "_ExportShadingNode: skipping '%s' because it's type '%s' is not Pxr.\n",
                depNode.name().asChar(),
                risShaderType.c_str()).c_str());
            return UsdPrim();
        }

        // The first node is the Bxdf, subsequent ones are Pattern objects
        UsdRiRisObject risObj;
        if (isFirstNode)
            risObj = UsdRiRisBxdf::Define(stage, shaderPath);
        else
            risObj = UsdRiRisPattern::Define(stage, shaderPath);

        risObj.CreateFilePathAttr(VtValue(SdfAssetPath(risShaderType)));

        MStatus status = MS::kFailure;

        for (unsigned int i = 0; i < depNode.attributeCount(); i++) {
            MPlug attrPlug = depNode.findPlug(depNode.attribute(i), true);
            if (attrPlug.isProcedural()) {
                // maya docs says these should not be saved off.  we skip them
                // here.
                continue;
            }

            if (attrPlug.isChild()) {
                continue;
            }

            // this is writing out things that live on the MFnDependencyNode.
            // maybe that's OK?  nothing downstream cares about it.

            TfToken attrName = TfToken(context.GetStandardAttrName(attrPlug));
            SdfValueTypeName attrTypeName = PxrUsdMayaWriteUtil::GetUsdTypeName(attrPlug);
            if (!attrTypeName) {
                // unsupported type
                continue;
            }

            UsdShadeInput input= risObj.CreateInput(attrName, attrTypeName);
            if (!input) {
                continue;
            }

            PxrUsdMayaWriteUtil::SetUsdAttr(
                attrPlug,
                input.GetAttr(),
                UsdTimeCode::Default());

            if (attrPlug.isConnected() && attrPlug.isDestination()) {
                MPlug connected(PxrUsdMayaUtil::GetConnected(attrPlug));
                MFnDependencyNode c(connected.node(), &status);
                if (status) {
                    if (UsdPrim cPrim = _ExportShadingNode(materialPrim,
                                                           c,
                                                           context,
                                                           processedPaths,
                                                           false)) {
                        UsdShadeConnectableAPI::ConnectToSource(input,
                            UsdShadeShader(cPrim),
                            TfToken(context.GetStandardAttrName(connected)));
                    }
                }
            }
        }

        return risObj.GetPrim();
    }

    void Export(const PxrUsdMayaShadingModeExportContext& context) override {
        MStatus status;
        const PxrUsdMayaShadingModeExportContext::AssignmentVector &assignments =
            context.GetAssignments();
        if (assignments.empty()) {
            return;
        }

        UsdPrim materialPrim = context.MakeStandardMaterialPrim(assignments);
        if (!materialPrim) {
            return;
        }

        MFnDependencyNode ssDepNode(context.GetSurfaceShader(), &status);
        if (!status) {
            return;
        }
        SdfPathSet  processedShaders;
        if (UsdPrim shaderPrim = _ExportShadingNode(materialPrim,
                                                    ssDepNode,
                                                    context,
                                                    &processedShaders, true)) {
            UsdRiMaterialAPI(materialPrim).SetBxdfSource(shaderPrim.GetPath());
        }
    }
};
}

TF_REGISTRY_FUNCTION_WITH_TAG(PxrUsdMayaShadingModeExportContext, pxrRis)
{
    PxrUsdMayaShadingModeRegistry::GetInstance().RegisterExporter("pxrRis", []() -> PxrUsdMayaShadingModeExporterPtr {
        return PxrUsdMayaShadingModeExporterPtr(
            static_cast<PxrUsdMayaShadingModeExporter*>(new PxrRisShadingModeExporter()));
    });
}

namespace _importer {

static MObject
_CreateShaderObject(
        const UsdRiRisObject& risShader, 
        PxrUsdMayaShadingModeImportContext* context);

static MObject
_GetOrCreateShaderObject(
        const UsdRiRisObject& risShader,
        PxrUsdMayaShadingModeImportContext* context)
{
    MObject shaderObj; 
    if (context->GetCreatedObject(risShader.GetPrim(), &shaderObj)) {
        return shaderObj;
    }

    shaderObj = _CreateShaderObject(risShader, context);
    return context->AddCreatedObject(risShader.GetPrim(), shaderObj);
}

// XXX: i think this belongs inside px_usdIO or somewhere else.
static MPlug
_ImportAttr(
        const UsdAttribute& usdAttr,
        const MFnDependencyNode& fnDep)
{
    const std::string mayaAttrName = usdAttr.GetName().GetString();
    MStatus status;

    MPlug mayaAttrPlug = fnDep.findPlug(mayaAttrName.c_str(), &status);
    if (!status) {
        return MPlug();
    }

    PxrUsdMayaUtil::setPlugValue(usdAttr, mayaAttrPlug);
    return mayaAttrPlug;
}

// Should only be called by _GetOrCreateShaderObject, no one else.
MObject
_CreateShaderObject(
        const UsdRiRisObject& risShader, 
        PxrUsdMayaShadingModeImportContext* context)
{
    MFnDependencyNode fnDep;
    // get ready for some recursion on UsdRiRisObject.  we could
    // also just save bxdf as that from the beginning.
    SdfAssetPath filePath;
    risShader.GetFilePathAttr().Get(&filePath);
    std::string mayaTypeStr = filePath.GetAssetPath();
    
    // Now remap the typeStr if found in the RIS table
    for (size_t i=0;i<_RFM_RISNODE_TABLE.size();i++) {
        if (_RFM_RISNODE_TABLE[i].second==mayaTypeStr) {
            mayaTypeStr=_RFM_RISNODE_TABLE[i].first;
            break;
        }
    }

    MStatus status;
    MObject shaderObj = fnDep.create(MString(mayaTypeStr.c_str()),
            MString(risShader.GetPrim().GetName().GetText()),
            &status);

    if (!status) {
        // we need to make sure assumes those types are loaded..
        MGlobal::displayError(TfStringPrintf(
                "Could not create node of type '%s' for shader '%s'. "
                "Probably missing a loadPlugin.\n",
                mayaTypeStr.c_str(),
                risShader.GetPrim().GetName().GetText()).c_str());
        return MObject();
    }

    // The rest of this is not really RIS specific at all.
    for (const UsdShadeInput &input : risShader.GetInputs()) {
        MPlug mayaAttr = _ImportAttr(input.GetAttr(), fnDep);
        if (mayaAttr.isNull()) {
            continue;
        }

        UsdShadeConnectableAPI source;
        TfToken sourceOutputName;
        UsdShadeAttributeType sourceType;
        // follow shader connections and recurse.
        if (UsdShadeConnectableAPI::GetConnectedSource(input, &source, 
                &sourceOutputName, &sourceType)) {
            if (UsdRiRisObject sourceRisObj = UsdRiRisObject(source.GetPrim())) {
                MObject sourceObj = _GetOrCreateShaderObject(sourceRisObj, context);
                MFnDependencyNode sourceDep(sourceObj, &status);
                if (!status) {
                    continue;
                }

                MPlug srcAttr = sourceDep.findPlug(sourceOutputName.GetText());
                PxrUsdMayaUtil::Connect(srcAttr, mayaAttr, false);
            }
        }
    }

    return shaderObj;
}

}; // namespace _importer

DEFINE_SHADING_MODE_IMPORTER(pxrRis, context)
{
    // This expects the renderman for maya plugin is loaded.
    // How do we ensure that it is?
    const UsdShadeMaterial& shadeMaterial = context->GetShadeMaterial();

    MStatus status;
    if (UsdRiRisBxdf bxdf = UsdRiMaterialAPI(shadeMaterial).GetBxdf()) {
        MObject bxdfObj = _importer::_GetOrCreateShaderObject(bxdf, context);
        MFnDependencyNode bxdfDep(bxdfObj, &status);
        MPlug ret = bxdfDep.findPlug("outColor", &status);
        if (status) {
            return ret;
        }
    }

    return MPlug();
}

PXR_NAMESPACE_CLOSE_SCOPE
