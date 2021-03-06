
#include "SurfaceAttach.h"

#include <maya/MPlug.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MGlobal.h>
#include <maya/MEulerRotation.h>


MTypeId SurfaceAttach::id(0x00121BC4);

MObject SurfaceAttach::surface;        
MObject SurfaceAttach::parentInverse;
MObject SurfaceAttach::samples;
MObject SurfaceAttach::staticLength;
MObject SurfaceAttach::offset;
MObject SurfaceAttach::genus;
MObject SurfaceAttach::reverse;

MObject SurfaceAttach::inU;
MObject SurfaceAttach::inV;
MObject SurfaceAttach::inUV;        

MObject SurfaceAttach::translateX;
MObject SurfaceAttach::translateY;
MObject SurfaceAttach::translateZ;
MObject SurfaceAttach::translate;

MObject SurfaceAttach::rotateX;
MObject SurfaceAttach::rotateY;
MObject SurfaceAttach::rotateZ;
MObject SurfaceAttach::rotate;

MObject SurfaceAttach::out;



SurfaceAttach::SurfaceAttach() {
	this->samplePoints = { MPoint(), MPoint() };
}

SurfaceAttach::~SurfaceAttach(){}

void* SurfaceAttach::creator() {
	return new SurfaceAttach();
}



MStatus SurfaceAttach::compute(const MPlug& plug, MDataBlock& dataBlock) {	
	MStatus stat;


	//TODO: fmod the UV inputs from the node to keep them betwen 0-1


	//TODO: make this not assume a 0-1 UV space



	if (plug == translate || plug == rotate){

		int dataSamples = dataBlock.inputValue(SurfaceAttach::samples).asInt();
		short dataGenus = (dataBlock.inputValue(SurfaceAttach::genus).asShort());
		double dataOffset = dataBlock.inputValue(SurfaceAttach::offset).asDouble();
		bool dataReverse = dataBlock.inputValue(SurfaceAttach::reverse).asBool();
		double dataStaticLength = dataBlock.inputValue(SurfaceAttach::staticLength).asDouble();
		MMatrix dataParentInverse = dataBlock.inputValue(SurfaceAttach::parentInverse).asMatrix();
		
		MFnNurbsSurface fnSurface (dataBlock.inputValue(SurfaceAttach::surface).asNurbsSurface());
		MFnDependencyNode fnDepend (plug.node());

		// Set UV Inputs
		this->inUVs(fnDepend);

		// If Percentage or Fixed Length
		if (dataGenus > 0){

			// If the amount of samples has changed, rebuild the arrays/data
			if (this->sampleCount != dataSamples) {
				this->allocate(dataSamples);
				this->sampleCount = dataSamples;
			}

			// Calculate Lengths
			this->surfaceLengths(fnSurface, 0.5);
		}	
		
		// Set all the existing out Plugs
		this->setOutPlugs(dataBlock, fnDepend, fnSurface, dataOffset, dataReverse, dataGenus, dataStaticLength, dataParentInverse);

		return MS::kSuccess;
	}

	
	return MS::kUnknownParameter;
}



void SurfaceAttach::inUVs(MFnDependencyNode &fn) {

	MPlug inUVPlug = fn.findPlug("inUV");
	unsigned int numElements = inUVPlug.numElements();

	this->uvInputs.resize((size_t)(numElements));

	for (unsigned int i=0; i < numElements; i++){
		MPlug uvPlug = inUVPlug.elementByLogicalIndex(i);
		std::array<double, 2> parms {uvPlug.child(0).asDouble(), uvPlug.child(1).asDouble()};
		uvInputs[i] = (parms);
	}
}


void SurfaceAttach::allocate(const int dataSamples) {

	this->sampler.clear();
	this->distances.clear();

	this->sampler.resize((size_t)(dataSamples));
	this->distances.resize((size_t)(dataSamples));

	double sample = 0.0;
	for (int i=0; i < dataSamples; i++){
		sample = (i + 1.0) / dataSamples;
		this->sampler[i] = sample;
		std::array<double, 2> dists {0.0, sample};
		this->distances[i] = dists;
	}
}


void SurfaceAttach::surfaceLengths(const MFnNurbsSurface &fnSurface, const double parmV) {

	MPoint pointA = samplePoints[0];
	fnSurface.getPointAtParam(0.0, parmV, pointA, MSpace::Space::kWorld);

	length = 0.0;
	MPoint pointB = samplePoints[1];

	for (unsigned int i=0; i < sampler.size(); i++){
		fnSurface.getPointAtParam(sampler[i], parmV, pointB, MSpace::Space::kWorld);

		// Add the measured distanced to 'length' and set the measured length in 'distances'		
		length += pointA.distanceTo(pointB);
		this->distances[i][0] = length;

		pointA.x = pointB.x;
		pointA.y = pointB.y;
		pointA.z = pointB.z;
	}
}


void SurfaceAttach::setOutPlugs(MDataBlock dataBlock, const MFnDependencyNode &fn, const MFnNurbsSurface &fnSurface,
								const double &dataOffset, const bool &dataReverse, const short &dataGenus,
								const double &dataStaticLength, const MMatrix &dataParentInverse) {

	MPlug outPlug = fn.findPlug("out");

	MIntArray plugIndices;
	outPlug.getExistingArrayAttributeIndices(plugIndices);

	MTransformationMatrix tfm;
	MVector t;
	MEulerRotation r;
	MPlug plugParent;
	for (unsigned int i=0; i < plugIndices.length(); i++){
		plugParent = outPlug.elementByLogicalIndex((unsigned int)plugIndices[i]);

		// Get Transformations
		tfm = this->matrix(fnSurface, plugIndices[i], dataOffset, dataReverse, dataGenus, dataStaticLength, dataParentInverse);
		t = tfm.translation(MSpace::Space::kWorld);
		r = tfm.eulerRotation();
		
		// Set Translate
		MPlug translatePlug = plugParent.child(0);
		translatePlug.child(0).setDouble(t.x);
		translatePlug.child(1).setDouble(t.y);
		translatePlug.child(2).setDouble(t.z);

		// Set Rotate
		MPlug rotatePlug = plugParent.child(1);
		rotatePlug.child(0).setDouble(r.x);
		rotatePlug.child(1).setDouble(r.y);
		rotatePlug.child(2).setDouble(r.z);

		// Mark Clean
		dataBlock.setClean(translatePlug);
		dataBlock.setClean(rotatePlug);
	}
}


MTransformationMatrix SurfaceAttach::matrix(const MFnNurbsSurface &fnSurface, const int plugID, const double &dataOffset,
											const bool &dataReverse, const short &dataGenus,
											const double &dataStaticLength, const MMatrix &dataParentInverse) {
	// Do all the Fancy stuff to input UV values
	double parmU, parmV;
	this->calculateUV(plugID, dataOffset, dataReverse, dataGenus, dataStaticLength, parmU, parmV);

	// Calculate transformations from UV values
	MVector normal = fnSurface.normal(parmU, parmV, MSpace::Space::kWorld);

	MPoint point;
	fnSurface.getPointAtParam(parmU, parmV, point, MSpace::Space::kWorld);

	MVector tanU, tanV;
	fnSurface.getTangents(parmU, parmV, tanU, tanV, MSpace::Space::kWorld);

	MVector posVec = MVector(point);

	const double dubArray[4][4] = { tanU.x, tanU.y, tanU.z, 0.0,
							  		normal.x, normal.y, normal.z, 0.0,
							  		tanV.x, tanV.y, tanV.z, 0.0,
							  		point.x, point.y, point.z, 1.0 };
	MMatrix mat (dubArray);

	return MTransformationMatrix (mat * dataParentInverse);
}


void SurfaceAttach::calculateUV(const int plugID, const double &dataOffset, const double &dataReverse,
								const short &dataGenus, const double &dataStaticLength,
								double &parmU, double &parmV) {

	parmU = this->uvInputs[plugID][0];
	parmV = this->uvInputs[plugID][1];

	// Offset
	double sum = parmU + dataOffset;
	if (sum >= 0.0)
		parmU = fmod(sum, 1.0);
	else
		parmU = 1.0 - fmod(1.0 - sum, 1.0);

	// Fix Flipping from 1.0 to 0.0
	if (uvInputs[plugID][0] == 1.0 && parmU == 0.0)
		parmU = 1.0;
	
	// Reverse
	if (dataReverse)
		parmU = 1.0 - parmU;
	
	// Percentage
	if (dataGenus > 0){
		double ratio = 1.0;		

		// Fixed Length
		if (dataGenus == 2){
			if (dataStaticLength < length)
				{ratio = dataStaticLength / length;}
		}
		
		double uLength = length * (parmU * ratio);
		parmU = this->uParmFromLength(uLength);
	}
}


double SurfaceAttach::uParmFromLength(const double distanceU) {
	size_t index = this->binSearch(distanceU);

	double distA = distances[index][0];
	double uA = distances[index][1];

	double distB = distances[index+1][0];
	double uB = distances[index+1][1];

	double distRatio = (distanceU - distA) / (distB - distA);

	return uA + ((uB - uA) * distRatio);
}


size_t SurfaceAttach::binSearch(const double distanceU) {
	size_t a = 0;
	size_t b = distances.size()-1;
	size_t c = 0;

	//Don't loop longer than the size of distances
	for (size_t i=0; i < distances.size(); i++){
	
		size_t pivot = (a + b) / 2;

		if (distances[pivot][0] == distanceU)
			c = pivot;
		
		else if (b - a == 1)
			c = a;

		else if (distances[pivot][0] < distanceU)
			a = pivot;

		else
			b = pivot;
	}

	return c;
}


MStatus SurfaceAttach::initialize() {
	MFnTypedAttribute fnTypeAttr;
	MFnNumericAttribute fnNumAttr;
	MFnUnitAttribute fnUnitAttr;
	MFnCompoundAttribute fnCompoundAttr;
	MFnEnumAttribute fnEnumAttr;
	MFnMatrixAttribute fnMatAttr;

	MStatus stat;

	// Input Attributes
	surface = fnTypeAttr.create("surface", "surface", MFnData::kNurbsSurface);

	parentInverse = fnMatAttr.create("parentInverse", "ps", MFnMatrixAttribute::kDouble);
	fnMatAttr.setKeyable(true);

	samples = fnNumAttr.create("samples", "samples", MFnNumericData::kInt, 2000);
	fnNumAttr.setKeyable(true);
	fnNumAttr.setMin(1.0);

	staticLength = fnNumAttr.create("staticLength", "staticLength", MFnNumericData::kDouble, 0.0001);
	fnNumAttr.setKeyable(true);
	fnNumAttr.setMin(0.0001);

	offset = fnNumAttr.create("offset", "offset", MFnNumericData::kDouble, 0.0);
	fnNumAttr.setKeyable(true);

	genus = fnEnumAttr.create("type", "type", 0);
	fnEnumAttr.addField("Parametric", 0);
	fnEnumAttr.addField("Percentage", 1); 
	fnEnumAttr.addField("FixedLength", 2); 
	fnEnumAttr.setKeyable(true); 

	reverse = fnNumAttr.create("reverse", "reverse", MFnNumericData::kBoolean, false);
	fnNumAttr.setKeyable(true);

	inU = fnNumAttr.create("inU", "U", MFnNumericData::kDouble, 0.5);
	fnNumAttr.setKeyable(true);

	inV = fnNumAttr.create("inV", "V", MFnNumericData::kDouble, 0.5);
	fnNumAttr.setKeyable(true);

	inUV = fnCompoundAttr.create("inUV", "inUV");
 	fnCompoundAttr.setKeyable(true);
 	fnCompoundAttr.setArray(true);
	fnCompoundAttr.addChild(inU);
	fnCompoundAttr.addChild(inV);
	fnCompoundAttr.setUsesArrayDataBuilder(true);

	// Output Attributes
	translateX = fnNumAttr.create("translateX", "translateX", MFnNumericData::kDouble);
	fnNumAttr.setWritable(false);
	fnNumAttr.setStorable(false);

	translateY = fnNumAttr.create("translateY", "translateY", MFnNumericData::kDouble);
	fnNumAttr.setWritable(false);
	fnNumAttr.setStorable(false);

	translateZ = fnNumAttr.create("translateZ", "translateZ", MFnNumericData::kDouble);
	fnNumAttr.setWritable(false);
	fnNumAttr.setStorable(false);

	translate = fnNumAttr.create("translate", "translate", translateX, translateY, translateZ);
	fnNumAttr.setWritable(false);
	fnNumAttr.setStorable(false);

	rotateX = fnUnitAttr.create("rotateX", "rotateX", MFnUnitAttribute::kAngle);
	fnUnitAttr.setWritable(false);
	fnUnitAttr.setStorable(false);

	rotateY = fnUnitAttr.create("rotateY", "rotateY", MFnUnitAttribute::kAngle);
	fnUnitAttr.setWritable(false);
	fnUnitAttr.setStorable(false);

	rotateZ = fnUnitAttr.create("rotateZ", "rotateZ", MFnUnitAttribute::kAngle);
	fnUnitAttr.setWritable(false);
	fnUnitAttr.setStorable(false);

	rotate = fnNumAttr.create("rotate", "rotate", rotateX, rotateY, rotateZ);
	fnNumAttr.setWritable(false);

	out = fnCompoundAttr.create("out", "out");
	fnCompoundAttr.setWritable(false);
	fnCompoundAttr.setArray(true);
	fnCompoundAttr.addChild(translate);
	fnCompoundAttr.addChild(rotate);
	fnCompoundAttr.setUsesArrayDataBuilder(true);


	// These aren't going to fail, give me a break :)

	// Add Attributes
	SurfaceAttach::addAttribute(surface);
	SurfaceAttach::addAttribute(parentInverse);
	SurfaceAttach::addAttribute(samples);
	SurfaceAttach::addAttribute(staticLength);
	SurfaceAttach::addAttribute(offset);
	SurfaceAttach::addAttribute(genus);	
	SurfaceAttach::addAttribute(reverse);
	SurfaceAttach::addAttribute(inUV);
	SurfaceAttach::addAttribute(out);

	// Attribute Affects
	SurfaceAttach::attributeAffects(surface, translate);
	SurfaceAttach::attributeAffects(parentInverse, translate);
	SurfaceAttach::attributeAffects(staticLength, translate);
	SurfaceAttach::attributeAffects(samples, translate);
	SurfaceAttach::attributeAffects(offset, translate);
	SurfaceAttach::attributeAffects(genus, translate);
	SurfaceAttach::attributeAffects(reverse, translate);
	SurfaceAttach::attributeAffects(inU, translate);
	SurfaceAttach::attributeAffects(inV, translate);
	
	SurfaceAttach::attributeAffects(surface, rotate);
	SurfaceAttach::attributeAffects(parentInverse, rotate);
	SurfaceAttach::attributeAffects(staticLength, rotate);
	SurfaceAttach::attributeAffects(samples, rotate);
	SurfaceAttach::attributeAffects(offset, rotate);
	SurfaceAttach::attributeAffects(genus, rotate);
	SurfaceAttach::attributeAffects(reverse, rotate);
	SurfaceAttach::attributeAffects(inU, rotate);
	SurfaceAttach::attributeAffects(inV, rotate);

	return MS::kSuccess;
}











