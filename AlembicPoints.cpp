#include "Alembic.h"
#include "AlembicPoints.h"
#include "AlembicXform.h"
#include "SceneEntry.h"
#include "SimpObj.h"
#include "Utility.h"
#include <IParticleObjectExt.h>
#include <ParticleFlow/IParticleChannelID.h>
#include <ParticleFlow/IParticleChannelShape.h>
#include <ParticleFlow/IParticleContainer.h>
#include <ParticleFlow/IParticleGroup.h>
#include <ParticleFlow/IPFSystem.h>

namespace AbcA = ::Alembic::AbcCoreAbstract::ALEMBIC_VERSION_NS;
namespace AbcB = ::Alembic::Abc::ALEMBIC_VERSION_NS;
using namespace AbcA;
using namespace AbcB;


AlembicPoints::AlembicPoints(const SceneEntry &in_Ref, AlembicWriteJob *in_Job)
    : AlembicObject(in_Ref, in_Job)
{
    std::string pointsName = in_Ref.node->GetName();
    std::string xformName = pointsName + "Xfo";

    Alembic::AbcGeom::OXform xform(GetOParent(), xformName.c_str(), GetCurrentJob()->GetAnimatedTs());
    Alembic::AbcGeom::OPoints points(xform, pointsName.c_str(), GetCurrentJob()->GetAnimatedTs());

    // create the generic properties
    mOVisibility = CreateVisibilityProperty(points, GetCurrentJob()->GetAnimatedTs());

    mXformSchema = xform.getSchema();
    mPointsSchema = points.getSchema();

    // create all properties
    mInstanceNamesProperty = OStringArrayProperty(mPointsSchema, ".instancenames", mPointsSchema.getMetaData(), GetCurrentJob()->GetAnimatedTs() );

    // particle attributes
    mScaleProperty = OV3fArrayProperty(mPointsSchema, ".scale", mPointsSchema.getMetaData(), GetCurrentJob()->GetAnimatedTs() );
    mOrientationProperty = OQuatfArrayProperty(mPointsSchema, ".orientation", mPointsSchema.getMetaData(), GetCurrentJob()->GetAnimatedTs() );
    mAngularVelocityProperty = OQuatfArrayProperty(mPointsSchema, ".angularvelocity", mPointsSchema.getMetaData(), GetCurrentJob()->GetAnimatedTs() );
    mAgeProperty = OFloatArrayProperty(mPointsSchema, ".age", mPointsSchema.getMetaData(), GetCurrentJob()->GetAnimatedTs() );
    mMassProperty = OFloatArrayProperty(mPointsSchema, ".mass", mPointsSchema.getMetaData(), GetCurrentJob()->GetAnimatedTs() );
    mShapeTypeProperty = OUInt16ArrayProperty(mPointsSchema, ".shapetype", mPointsSchema.getMetaData(), GetCurrentJob()->GetAnimatedTs() );
    mShapeTimeProperty = OFloatArrayProperty(mPointsSchema, ".shapetime", mPointsSchema.getMetaData(), GetCurrentJob()->GetAnimatedTs() );
    mShapeInstanceIDProperty = OUInt16ArrayProperty(mPointsSchema, ".shapeinstanceid", mPointsSchema.getMetaData(), GetCurrentJob()->GetAnimatedTs() );
    mColorProperty = OC4fArrayProperty(mPointsSchema, ".color", mPointsSchema.getMetaData(), GetCurrentJob()->GetAnimatedTs() );
}

AlembicPoints::~AlembicPoints()
{
    // we have to clear this prior to destruction this is a workaround for issue-171
    mOVisibility.reset();
}

Alembic::Abc::OCompoundProperty AlembicPoints::GetCompound()
{
    return mPointsSchema;
}

bool AlembicPoints::Save(double time)
{
    // Note: Particles are always considered to be animated even though
    //       the node does not have the IsAnimated() flag.

    // Extract our particle emitter at the given time
    TimeValue ticks = GetTimeValueFromFrame(time);
    Object *obj = GetRef().node->EvalWorldState(ticks).obj;
    IPFSystem *particleSystem = PFSystemInterface(obj);
    if (particleSystem == NULL)
    {
        return false;
    }

    IParticleObjectExt *particlesExt = GetParticleObjectExtInterface(obj);
    if (particlesExt == NULL)
    {
        return false;
    }

    particlesExt->UpdateParticles(GetRef().node, ticks);
    int numParticles = particlesExt->NumParticles();

    // Set the visibility
    float flVisibility = GetRef().node->GetLocalVisibility(ticks);
    mOVisibility.set(flVisibility > 0 ? Alembic::AbcGeom::kVisibilityVisible : Alembic::AbcGeom::kVisibilityHidden);

    float masterScaleUnitMeters = (float)GetMasterScale(UNITS_METERS);

    // Store positions, velocity, width/size, scale, id, bounding box
    std::vector<Alembic::Abc::V3f> positionVec(numParticles);
    std::vector<Alembic::Abc::V3f> velocityVec(numParticles);
    std::vector<Alembic::Abc::V3f> scaleVec(numParticles);
    std::vector<float> widthVec(numParticles);
    std::vector<float> ageVec(numParticles);
    std::vector<float> massVec;
    std::vector<float> shapeTimeVec;
    std::vector<uint64_t> idVec(numParticles);
    std::vector<uint16_t> shapeTypeVec;
    std::vector<uint16_t> shapeInstanceIDVec;
    std::vector<Alembic::Abc::Quatf> orientationVec(numParticles);
    std::vector<Alembic::Abc::Quatf> angularVelocityVec(numParticles);
    std::vector<Alembic::Abc::C4f> colorVec;
    std::vector<std::string> instanceNamesVec;
    Alembic::Abc::Quatf orientation;
    Alembic::Abc::Quatf spin;
    Alembic::Abc::Box3d bbox;
    bool constantPos = true;
    bool constantVel = true;
    bool constantScale = true;
    bool constantWidth = true;
    bool constantAge = true;
    bool constantOrientation = true;
    bool constantAngularVel = true;

    for (int i = 0; i < numParticles; ++i)
    {
        Imath::V3f pos = ConvertMaxPointToAlembicPoint(*particlesExt->GetParticlePositionByIndex(i), masterScaleUnitMeters);
        Imath::V3f vel = ConvertMaxVectorToAlembicVector(*particlesExt->GetParticleSpeedByIndex(i), masterScaleUnitMeters, true);
        Imath::V3f scale = ConvertMaxVectorToAlembicVector(*particlesExt->GetParticleScaleXYZByIndex(i), masterScaleUnitMeters, false);
        float width = particlesExt->GetParticleScaleByIndex(i) * GetInchesToDecimetersRatio( masterScaleUnitMeters );
        TimeValue age = particlesExt->GetParticleAgeByIndex(i);
        uint64_t id = particlesExt->GetParticleBornIndex(i);
        ConvertMaxEulerXYZToAlembicQuat(*particlesExt->GetParticleOrientationByIndex(i), &orientation);
        ConvertMaxAngAxisToAlembicQuat(*particlesExt->GetParticleSpinByIndex(i), &spin);

        /*
        Mesh *mesh = particlesExt->GetParticleShapeByIndex(i);
        INode *particleGroupNode = particlesExt->GetParticleGroup(i);
        Object *particleGroupObj = (particleGroupNode != NULL) ? particleGroupNode->EvalWorldState(ticks).obj : NULL;
        IParticleGroup *particleGroup = GetParticleGroupInterface(particleGroupObj);
        ::IObject *particleContainerObject = (particleGroup != NULL) ? particleGroup->GetParticleContainer() : NULL;
        IParticleChannelIDR *chID = GetParticleChannelIDRInterface(particleContainerObject);
        if (chID != NULL)
        {
            id = chID->GetParticleIndex(i);
        }
        IParticleChannelMeshR *channelMesh = GetParticleChannelShapeRInterface(particleContainerObject);
        if (channelMesh != NULL)
        {
            bool isShared = channelMesh->IsShared();
            const Mesh *mesh = channelMesh->GetValue(i);
        }
        */

        positionVec[i].setValue(pos.x, pos.y, pos.z);
        velocityVec[i].setValue(vel.x, vel.y, vel.z);
        scaleVec[i].setValue(scale.x, scale.y, scale.z);
        widthVec[i] = width;
        ageVec[i] = static_cast<float>(GetSecondsFromTimeValue(age));
        idVec[i] = id;
        orientationVec[i] = orientation;
        angularVelocityVec[i] = spin;
        bbox.extendBy(positionVec[i]);

        constantPos &= (positionVec[i] == positionVec[0]);
        constantVel &= (velocityVec[i] == velocityVec[0]);
        constantScale &= (scaleVec[i] == scaleVec[0]);
        constantWidth &= (widthVec[i] == widthVec[0]);
        constantAge &= (ageVec[i] == ageVec[0]);
        constantOrientation &= (orientationVec[i] == orientationVec[0]);
        constantAngularVel &= (angularVelocityVec[i] == angularVelocityVec[0]);
    }

    // Set constant properties that are not currently supported by Max
    massVec.push_back(1.0f);
    shapeTimeVec.push_back(1.0f);
    shapeTypeVec.push_back(ShapeType_Point);
    shapeInstanceIDVec.push_back(0);
    colorVec.push_back(Alembic::Abc::C4f(0.0f, 0.0f, 0.0f, 1.0f));
    instanceNamesVec.push_back("");

    if (numParticles == 0)
    {
        positionVec.push_back(Alembic::Abc::V3f(FLT_MAX, FLT_MAX, FLT_MAX));
        velocityVec.push_back(Alembic::Abc::V3f(0.0f, 0.0f, 0.0f));
        scaleVec.push_back(Alembic::Abc::V3f(1.0f, 1.0f, 1.0f));
        widthVec.push_back(0.0f);
        ageVec.push_back(0.0f);
        idVec.push_back(static_cast<uint64_t>(-1));
        orientationVec.push_back(Alembic::Abc::Quatf(0.0f, 1.0f, 0.0f, 0.0f));
        angularVelocityVec.push_back(Alembic::Abc::Quatf(0.0f, 1.0f, 0.0f, 0.0f));
    }
    else
    {
        if (constantPos)        { positionVec.resize(1); }
        if (constantVel)        { velocityVec.resize(1); }
        if (constantScale)      { scaleVec.resize(1); }
        if (constantWidth)      { widthVec.resize(1); }
        if (constantAge)        { ageVec.resize(1); }
        if (constantOrientation){ orientationVec.resize(1); }
        if (constantAngularVel) { angularVelocityVec.resize(1); }
    }

    // Store the information into our properties and points schema
    Alembic::Abc::P3fArraySample positionSample(&positionVec.front(), positionVec.size());
    Alembic::Abc::P3fArraySample velocitySample(&velocityVec.front(), velocityVec.size());
    Alembic::Abc::P3fArraySample scaleSample(&scaleVec.front(), scaleVec.size());
    Alembic::Abc::FloatArraySample widthSample(&widthVec.front(), widthVec.size());
    Alembic::Abc::FloatArraySample ageSample(&ageVec.front(), ageVec.size());
    Alembic::Abc::FloatArraySample massSample(&massVec.front(), massVec.size());
    Alembic::Abc::FloatArraySample shapeTimeSample(&shapeTimeVec.front(), shapeTimeVec.size());
    Alembic::Abc::UInt64ArraySample idSample(&idVec.front(), idVec.size());
    Alembic::Abc::UInt16ArraySample shapeTypeSample(&shapeTypeVec.front(), shapeTypeVec.size());
    Alembic::Abc::UInt16ArraySample shapeInstanceIDSample(&shapeInstanceIDVec.front(), shapeInstanceIDVec.size());
    Alembic::Abc::QuatfArraySample orientationSample(&orientationVec.front(), orientationVec.size());
    Alembic::Abc::QuatfArraySample angularVelocitySample(&angularVelocityVec.front(), angularVelocityVec.size());
    Alembic::Abc::C4fArraySample colorSample(&colorVec.front(), colorVec.size());
    Alembic::Abc::StringArraySample instanceNamesSample(&instanceNamesVec.front(), instanceNamesVec.size());

    mScaleProperty.set(scaleSample);
    mAgeProperty.set(ageSample);
    mMassProperty.set(massSample);
    mShapeTimeProperty.set(shapeTimeSample);
    mShapeTypeProperty.set(shapeTypeSample);
    mShapeInstanceIDProperty.set(shapeInstanceIDSample);
    mOrientationProperty.set(orientationSample);
    mAngularVelocityProperty.set(angularVelocitySample);
    mColorProperty.set(colorSample);
    mInstanceNamesProperty.set(instanceNamesSample);

    mPointsSample.setPositions(positionSample);
    mPointsSample.setVelocities(velocitySample);
    mPointsSample.setWidths(Alembic::AbcGeom::OFloatGeomParam::Sample(widthSample, Alembic::AbcGeom::kVertexScope));
    mPointsSample.setIds(idSample);
    mPointsSample.setSelfBounds(bbox);

    mPointsSchema.set(mPointsSample);

    mNumSamples++;

    return true;
}

// static
void AlembicPoints::ConvertMaxEulerXYZToAlembicQuat(const Point3 &degrees, Alembic::Abc::Quatf &quat)
{
    // Get the angles as a float vector of radians
    float angles[] = { DEG_TO_RAD * degrees.x, DEG_TO_RAD * degrees.y, DEG_TO_RAD * degrees.z };

    // Convert the angles to a quaternion
    Quat maxQuat;
    EulerToQuat(angles, maxQuat, EULERTYPE_XYZ);

    // Convert the quaternion to an angle and axis
    AngAxis maxAngAxis(maxQuat);

    ConvertMaxAngAxisToAlembicQuat(maxAngAxis, quat);
}

// static
void AlembicPoints::ConvertMaxAngAxisToAlembicQuat(const AngAxis &angAxis, Alembic::Abc::Quatf &quat)
{
    Point3 alembicAxis;
    ConvertMaxNormalToAlembicNormal(angAxis.axis, alembicAxis);

    quat.v.x = alembicAxis.x;
    quat.v.y = alembicAxis.y;
    quat.v.z = alembicAxis.z;
    quat.r = angAxis.angle;
}
