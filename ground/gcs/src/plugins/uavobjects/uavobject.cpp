/**
 ******************************************************************************
 *
 * @file       uavobject.cpp
 *
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2014
 * @author     dRonin, http://dRonin.org/, Copyright (C) 2016
 *
 * @see        The GNU Public License (GPL) Version 3
 * @addtogroup GCSPlugins GCS Plugins
 * @{
 * @addtogroup UAVObjectsPlugin UAVObjects Plugin
 * @{
 * @brief      The UAVUObjects GCS plugin
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 3 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License 
 * for more details.
 * 
 * You should have received a copy of the GNU General Public License along 
 * with this program; if not, see <http://www.gnu.org/licenses/>
 *
 * Additional note on redistribution: The copyright and license notices above
 * must be maintained in each individual source file that is a derivative work
 * of this source file; otherwise redistribution is prohibited.
 */

#include "uavobject.h"
#include <QtEndian>
#include <QDebug>
#include <QJsonArray>
#include <QJsonValue>

// Constants
#define UAVOBJ_ACCESS_SHIFT 0
#define UAVOBJ_GCS_ACCESS_SHIFT 1
#define UAVOBJ_TELEMETRY_ACKED_SHIFT 2
#define UAVOBJ_GCS_TELEMETRY_ACKED_SHIFT 3
#define UAVOBJ_TELEMETRY_UPDATE_MODE_SHIFT 4
#define UAVOBJ_GCS_TELEMETRY_UPDATE_MODE_SHIFT 6
#define UAVOBJ_UPDATE_MODE_MASK 0x3

// Macros
#define SET_BITS(var, shift, value, mask) var = (var & ~(mask << shift)) |	(value << shift);

/**
 * Constructor
 * @param objID The object ID
 * @param isSingleInst True if this object can only have a single instance
 * @param name Object name
 */
UAVObject::UAVObject(quint32 objID, bool isSingleInst, const QString& name)
{
    this->objID = objID;
    this->instID = 0;
    this->isSingleInst = isSingleInst;
    this->name = name;
}

/**
 * Initialize object with its instance ID
 */
void UAVObject::initialize(quint32 instID)
{
    this->instID = instID;
}

/**
 * Initialize objects' data fields
 * @param fields List of fields held by the object
 * @param data Pointer to that actual object data, this is needed by the fields to access the data
 * @param numBytes Number of bytes in the object (total, including all fields)
 */
void UAVObject::initializeFields(QList<UAVObjectField*>& fields, quint8* data, quint32 numBytes)
{
    this->numBytes = numBytes;
    this->data = data;
    this->fields = fields;
    // Initialize fields
    quint32 offset = 0;
    for (int n = 0; n < fields.length(); ++n)
    {
        fields[n]->initialize(data, offset, this);
        offset += fields[n]->getNumBytes();
        connect(fields[n], SIGNAL(fieldUpdated(UAVObjectField*)), this, SLOT(fieldUpdated(UAVObjectField*)));
    }
}

/**
 * Called from the fields each time they are updated
 */
void UAVObject::fieldUpdated(UAVObjectField* field)
{
    Q_UNUSED(field);
//    emit objectUpdatedAuto(this); // trigger object updated event
//    emit objectUpdated(this);
}

/**
 * Get the object ID
 */
quint32 UAVObject::getObjID()
{
    return objID;
}

/**
 * Get the instance ID
 */
quint32 UAVObject::getInstID()
{
    return instID;
}

/**
 * Returns true if this is a single instance object
 */
bool UAVObject::isSingleInstance()
{
    return isSingleInst;
}

/**
 * Get the name of the object
 */
QString UAVObject::getName()
{
    return name;
}

/**
 * Get the description of the object
 */
QString UAVObject::getDescription()
{
    return description;
}

/**
 * Set the description of the object
 */
void UAVObject::setDescription(const QString& description)
{
    this->description = description;
}

/**
 * Get the category of the object
 */
QString UAVObject::getCategory()
{
    return category;
}

/**
 * Set the category of the object
 */
void UAVObject::setCategory(const QString& category)
{
    this->category = category;
}


/**
 * Get the total number of bytes of the object's data
 */
quint32 UAVObject::getNumBytes()
{
    return numBytes;
}

/**
 * Request that this object is updated with the latest values from the autopilot
 */
void UAVObject::requestUpdate()
{
    emit updateRequested(this);
}

/**
 * Request that this object and all it's instances updated with the latest values from the autopilot
 */
void UAVObject::requestUpdateAllInstances()
{
    emit updateAllInstancesRequested(this);
}

/**
 * Signal that the object has been updated
 */
void UAVObject::updated()
{
    emit objectUpdatedManual(this);
    emit objectUpdated(this);
}

/**
 * Get the number of fields held by this object
 */
qint32 UAVObject::getNumFields()
{
    return fields.count();
}

/**
 * Get the object's fields
 */
QList<UAVObjectField*> UAVObject::getFields()
{
    return fields;
}

/**
 * Get the JSON representation of the object
 */
QJsonObject UAVObject::getJsonRepresentation() {

    QJsonObject obj, fieldMap;

    obj["id"] = static_cast <qint64> (getObjID());
    obj["name"] = getName();

    foreach (UAVObjectField* field, fields) {
        quint32 nelem = field->getNumElements();

        if (nelem > 1) {
            QVariantList vals;
            for (unsigned int n = 0; n < nelem; ++n) {
                vals.append(field->getValue(n));
            }

            fieldMap[field->getName()] = QJsonArray::fromVariantList(vals);
        } else {
            fieldMap[field->getName()] = QJsonValue::fromVariant(field->getValue(0));
        }
    }

    obj["fields"] = fieldMap;

    return obj;
}

/**
 * Get a specific field
 * @returns The field or NULL if not found
 */
UAVObjectField* UAVObject::getField(const QString& name)
{
    // Look for field
    for (int n = 0; n < fields.length(); ++n)
    {
        if (name.compare(fields[n]->getName()) == 0)
        {
            return fields[n];
        }
    }
    // If this point is reached then the field was not found
    qWarning()<<"UAVObject::getField Non existant field "<<name<<" requested.  This indicates a bug.  Make sure you also have null checking for non-debug code.";
    return NULL;
}

/**
 * Pack the object data into a byte array
 * @returns The number of bytes copied
 */
qint32 UAVObject::pack(quint8* dataOut)
{
    qint32 offset = 0;
    for (QList<UAVObjectField*>::iterator iter = fields.begin(); iter != fields.end(); ++iter)
    {
        UAVObjectField *field = *iter;
        field->pack(&dataOut[offset]);
        offset += field->getNumBytes();
    }
    return numBytes;
}

/**
 * Unpack the object data from a byte array
 * @returns The number of bytes copied
 */
qint32 UAVObject::unpack(const quint8* dataIn)
{
    qint32 offset = 0;
    for (QList<UAVObjectField*>::iterator iter = fields.begin(); iter != fields.end(); ++iter)
    {
        UAVObjectField *field = *iter;
        field->unpack(&dataIn[offset]);
        offset += field->getNumBytes();
    }
    emit objectUnpacked(this); // trigger object updated event
    emit objectUpdated(this);

    return numBytes;
}

/**
 * Return a string with the object information
 */
QString UAVObject::toString()
{
    QString sout;
    sout.append( toStringBrief() );
    sout.append( toStringData() );
    return sout;
}

/**
 * Return a string with the object information (only the header)
 */
QString UAVObject::toStringBrief()
{
    QString sout;
    sout.append( QString("%1 (ID: %2, InstID: %3, NumBytes: %4, SInst: %5)\n")
                 .arg(getName())
                 .arg(getObjID())
                 .arg(getInstID())
                 .arg(getNumBytes())
                 .arg(isSingleInstance()) );
    return sout;
}

/**
 * Return a string with the object information (only the data)
 */
QString UAVObject::toStringData()
{
    QString sout;
    sout.append("Data:\n");
    for (QList<UAVObjectField*>::iterator iter = fields.begin(); iter != fields.end(); ++iter)
    {
        UAVObjectField *field = *iter;
        sout.append( QString("\t%1").arg(field->toString()) );
    }
    return sout;
}

/**
 * (overloaded) Emit the transactionCompleted event (used by the UAVTalk plugin)
 */
void UAVObject::emitTransactionCompleted(bool success)
{
    emit transactionCompleted(this, success);
}

/**
 * (overloaded) Emit the transactionCompletedNack event
 */
void UAVObject::emitTransactionCompleted(bool success, bool nacked)
{
    emit transactionCompleted(this, success, nacked);
}

/**
 * Emit the newInstance event
 */
void UAVObject::emitNewInstance(UAVObject * obj)
{
    emit newInstance(obj);
}

/**
 * Emit the instanceRemoved event
 */
void UAVObject::emitInstanceRemoved(UAVObject * obj)
{
    emit instanceRemoved(obj);
}

/**
 * Initialize a default UAVObjMetadata object.
 * \param[in] metadata The metadata object
 */
void UAVObject::MetadataInitialize(UAVObject::Metadata& metadata)
{
	metadata.flags =
		ACCESS_READWRITE << UAVOBJ_ACCESS_SHIFT |
		ACCESS_READWRITE << UAVOBJ_GCS_ACCESS_SHIFT |
		1 << UAVOBJ_TELEMETRY_ACKED_SHIFT |
		1 << UAVOBJ_GCS_TELEMETRY_ACKED_SHIFT |
		UPDATEMODE_ONCHANGE << UAVOBJ_TELEMETRY_UPDATE_MODE_SHIFT |
		UPDATEMODE_ONCHANGE << UAVOBJ_GCS_TELEMETRY_UPDATE_MODE_SHIFT;
	metadata.flightTelemetryUpdatePeriod = 0;
	metadata.gcsTelemetryUpdatePeriod = 0;
	metadata.loggingUpdatePeriod = 0;
}

/**
 * Get the UAVObject metadata access member
 * \param[in] metadata The metadata object
 * \return the access type
 */
UAVObject::AccessMode UAVObject::GetFlightAccess(const UAVObject::Metadata& metadata)
{
	return UAVObject::AccessMode((metadata.flags >> UAVOBJ_ACCESS_SHIFT) & 1);
}

/**
 * Set the UAVObject metadata access member
 * \param[in] metadata The metadata object
 * \param[in] mode The access mode
 */
void UAVObject::SetFlightAccess(UAVObject::Metadata& metadata, UAVObject::AccessMode mode)
{
	SET_BITS(metadata.flags, UAVOBJ_ACCESS_SHIFT, mode, 1);
}

/**
 * Get the UAVObject metadata GCS access member
 * \param[in] metadata The metadata object
 * \return the GCS access type
 */
UAVObject::AccessMode UAVObject::GetGcsAccess(const UAVObject::Metadata& metadata)
{
	return UAVObject::AccessMode((metadata.flags >> UAVOBJ_GCS_ACCESS_SHIFT) & 1);
}

/**
 * Set the UAVObject metadata GCS access member
 * \param[in] metadata The metadata object
 * \param[in] mode The access mode
 */
void UAVObject::SetGcsAccess(UAVObject::Metadata& metadata, UAVObject::AccessMode mode) {
	SET_BITS(metadata.flags, UAVOBJ_GCS_ACCESS_SHIFT, mode, 1);
}

/**
 * Get the UAVObject metadata telemetry acked member
 * \param[in] metadata The metadata object
 * \return the telemetry acked boolean
 */
quint8 UAVObject::GetFlightTelemetryAcked(const UAVObject::Metadata& metadata) {
	return (metadata.flags >> UAVOBJ_TELEMETRY_ACKED_SHIFT) & 1;
}

/**
 * Set the UAVObject metadata telemetry acked member
 * \param[in] metadata The metadata object
 * \param[in] val The telemetry acked boolean
 */
void UAVObject::SetFlightTelemetryAcked(UAVObject::Metadata& metadata, quint8 val) {
	SET_BITS(metadata.flags, UAVOBJ_TELEMETRY_ACKED_SHIFT, val, 1);
}

/**
 * Get the UAVObject metadata GCS telemetry acked member
 * \param[in] metadata The metadata object
 * \return the telemetry acked boolean
 */
quint8 UAVObject::GetGcsTelemetryAcked(const UAVObject::Metadata& metadata) {
	return (metadata.flags >> UAVOBJ_GCS_TELEMETRY_ACKED_SHIFT) & 1;
}

/**
 * Set the UAVObject metadata GCS telemetry acked member
 * \param[in] metadata The metadata object
 * \param[in] val The GCS telemetry acked boolean
 */
void UAVObject::SetGcsTelemetryAcked(UAVObject::Metadata& metadata, quint8 val) {
	SET_BITS(metadata.flags, UAVOBJ_GCS_TELEMETRY_ACKED_SHIFT, val, 1);
}

/**
 * Get the UAVObject metadata telemetry update mode
 * \param[in] metadata The metadata object
 * \return the telemetry update mode
 */
UAVObject::UpdateMode UAVObject::GetFlightTelemetryUpdateMode(const UAVObject::Metadata& metadata) {
	return UAVObject::UpdateMode((metadata.flags >> UAVOBJ_TELEMETRY_UPDATE_MODE_SHIFT) & UAVOBJ_UPDATE_MODE_MASK);
}

/**
 * Set the UAVObject metadata telemetry update mode member
 * \param[in] metadata The metadata object
 * \param[in] val The telemetry update mode
 */
void UAVObject::SetFlightTelemetryUpdateMode(UAVObject::Metadata& metadata, UAVObject::UpdateMode val) {
	SET_BITS(metadata.flags, UAVOBJ_TELEMETRY_UPDATE_MODE_SHIFT, val, UAVOBJ_UPDATE_MODE_MASK);
}

/**
 * Get the UAVObject metadata GCS telemetry update mode
 * \param[in] metadata The metadata object
 * \return the GCS telemetry update mode
 */
UAVObject::UpdateMode UAVObject::GetGcsTelemetryUpdateMode(const UAVObject::Metadata& metadata) {
	return UAVObject::UpdateMode((metadata.flags >> UAVOBJ_GCS_TELEMETRY_UPDATE_MODE_SHIFT) & UAVOBJ_UPDATE_MODE_MASK);
}

/**
 * Set the UAVObject metadata GCS telemetry update mode member
 * \param[in] metadata The metadata object
 * \param[in] val The GCS telemetry update mode
 */
void UAVObject::SetGcsTelemetryUpdateMode(UAVObject::Metadata& metadata, UAVObject::UpdateMode val) {
	SET_BITS(metadata.flags, UAVOBJ_GCS_TELEMETRY_UPDATE_MODE_SHIFT, val, UAVOBJ_UPDATE_MODE_MASK);
}
