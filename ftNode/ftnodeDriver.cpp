/*
 * Copyright (C) 2019 iCub Facility
 * Authors: Yeshasvi Tirupachuri
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 */

#include "ftnodeDriver.h"

#include <yarp/os/LogStream.h>
#include <yarp/os/Bottle.h>
#include <yarp/os/Time.h>

#include <mutex>
#include <string>
#include <stdio.h>
#include <vector>

const std::string DeviceName = "ftnodeDriver";
const std::string LogPrefix = DeviceName + ":";
double period = 0.01;

using namespace yarp::dev;
using namespace yarp::os;

struct AnalogSensorData
{
    size_t numberOfChannels = 0;
    std::vector<double> measurements;
};

struct SerialPortWrenchData
{
    std::vector<double> forceBuffer;
    bool forceUpdateFlag;

    std::vector<double> torqueBuffer;
    bool torqueUpdateFlag;

};

class ftnodeDriver::Impl
{
public:
    mutable std::mutex mutex;
    yarp::dev::ISerialDevice *iSerialDevice = nullptr;

    std::string serialComPortName;

    // Buffer for storing data from serial port
    std::vector<SerialPortWrenchData> serialPortWrenchDataVector;

    int numberOfFTSensors;
    AnalogSensorData analogSensorData;
};


// Default constructor
ftnodeDriver::ftnodeDriver()
    : PeriodicThread(period)
    , pImpl{new Impl()}
{}

// Destructor
ftnodeDriver::~ftnodeDriver() = default;

bool ftnodeDriver::open(yarp::os::Searchable& config)
{
    // ==================================
    // Check the configuration parameters
    // ==================================

    // Period of the this device
    if (!(config.check("period") && config.find("period").isFloat64())) {
        yInfo() << LogPrefix << "Using default period: " << period << "s";
    }
    else {
        period = config.find("period").asFloat64();
        yInfo() << LogPrefix << "Using the period : " << period << "s";
    }

    // The number of sensors to be configured for IAnalogSensor interface
    // Currently, the serial port streams data from only one FT and in the
    // future it will also stream the data from the four FTs of the FTshoes
    if (!(config.check("numberOfFTSensors") && config.find("numberOfFTSensors").isInt())) {
        yError() << LogPrefix << "Option 'channels' not found or not a valid integer";
        return false;
    }

    // ==================================
    // Parse the configuration parameters
    // ==================================

    pImpl->numberOfFTSensors = config.find("numberOfFTSensors").asInt();

    // Initialize the serial port data buffers vector
    pImpl->serialPortWrenchDataVector.resize(pImpl->numberOfFTSensors);

    for (size_t i = 0; i < pImpl->numberOfFTSensors; i++) {
        pImpl->serialPortWrenchDataVector.at(i).forceBuffer.resize(3, 0.0);
        pImpl->serialPortWrenchDataVector.at(i).torqueBuffer.resize(3, 0.0);

        pImpl->serialPortWrenchDataVector.at(i).forceUpdateFlag = false;
        pImpl->serialPortWrenchDataVector.at(i).torqueUpdateFlag = false;
    }

    // Set the number of channels, 6 for each FT sensor
    pImpl->analogSensorData.numberOfChannels = pImpl->numberOfFTSensors * 6;

    // Resize the measurements buffer and initialize to zero
    pImpl->analogSensorData.measurements.resize(pImpl->analogSensorData.numberOfChannels, 0.0);

    return true;

}

void ftnodeDriver::run()
{
    char charMsg[5000];
    int size = pImpl->iSerialDevice->receiveLine(charMsg, 5000);

    yInfo() << LogPrefix << "Received char serial message size is " << size;

    std::string serialPortMsg(charMsg);
    yInfo() << LogPrefix << "Received char serial message is " << serialPortMsg;
    
    if (serialPortMsg.size() != 0) {

        // Get the received serial message as a string
        //std::string msgString = serialPortMsg.get(0).asString();
        yarp::os::Bottle extractedMsgBottle;

        // Extract each word
        std::stringstream iss(serialPortMsg);

        while (iss.good()) {

            std::string msg;
            getline(iss, msg, ',');

            // TODO: Check the precision during conversion from string to double
            double value = atof(msg.c_str());
            extractedMsgBottle.addFloat64(value);
            yInfo() << LogPrefix << "Extracted word : " << msg << " " << value;

        }

        yInfo() << LogPrefix << "Extracted message bottle size : " << extractedMsgBottle.size();
        yInfo() << LogPrefix << "Extracted message bottle data : " << extractedMsgBottle.toString().c_str();

        // Check the size of the extracted message
        // The incoming serial message expected structure is for two FTSensors
        // (fx/tx : MSB LSB) (fy/ty : MSB LSB) (fz/tz : MSB LSB) (canAddress) (force/torque)
        // So the total size of the data is : 32
        if (extractedMsgBottle.size() != 32) {
            yWarning() << LogPrefix << "Extracted serial message structure is incorrect."
                                       " Expected data structure is (fx/tx : MSB LSB) (fy/ty : MSB LSB) (fz/tz : MSB LSB) (canAddress) (force/torque) for"
                                       " two FTSensors. So, the total size of the serial message should be 32";
        }
        else
        {
            // Process first sub message
            for (size_t subMsgIndex = 0; subMsgIndex < 4; subMsgIndex++) {

                yInfo() << LogPrefix << "Sub msg index : " << subMsgIndex;
                yInfo() << LogPrefix << extractedMsgBottle.get(8 * subMsgIndex + 0).asFloat64() << " "
                        << extractedMsgBottle.get(8 * subMsgIndex + 1).asFloat64() << " "
                        << extractedMsgBottle.get(8 * subMsgIndex + 2).asFloat64() << " "
                        << extractedMsgBottle.get(8 * subMsgIndex + 3).asFloat64() << " "
                        << extractedMsgBottle.get(8 * subMsgIndex + 4).asFloat64() << " "
                        << extractedMsgBottle.get(8 * subMsgIndex + 5).asFloat64() << " "
                        << extractedMsgBottle.get(8 * subMsgIndex + 6).asFloat64() << " "
                        << extractedMsgBottle.get(8 * subMsgIndex + 7).asFloat64() << " ";

                // Get force/torque components
                unsigned int xcomponentMSB = extractedMsgBottle.get(8 * subMsgIndex + 0).asFloat64();
                unsigned int xcomponentLSB = extractedMsgBottle.get(8 * subMsgIndex + 1).asFloat64();

                unsigned int ycomponentMSB = extractedMsgBottle.get(8 * subMsgIndex + 2).asFloat64();
                unsigned int ycomponentLSB = extractedMsgBottle.get(8 * subMsgIndex + 3).asFloat64();

                unsigned int zcomponentMSB = extractedMsgBottle.get(8 * subMsgIndex + 4).asFloat64();
                unsigned int zcomponentLSB = extractedMsgBottle.get(8 * subMsgIndex + 5).asFloat64();

                // Compute correct values based on MSB LSB
                double xcomponent = xcomponentMSB * 256 + xcomponentLSB;
                double ycomponent = ycomponentMSB * 256 + ycomponentLSB;
                double zcomponent = zcomponentMSB * 256 + zcomponentLSB;

                // Get CAN address or wrench source number
                // FTShoes has CAN address as [1 2 3 4]
                // In case this changes, we need to consider a mapping through the parameter configuration
                int wrenchSourceNumber = extractedMsgBottle.get(8 * subMsgIndex + 6).asFloat64();

                yInfo() << LogPrefix << "Wrench source : " << wrenchSourceNumber;

                if (wrenchSourceNumber > pImpl->numberOfFTSensors) {
                    yWarning() << LogPrefix << "CANAddress or wrench source number from the serial message is more than"
                                               " the wrench source number specified in the configuration file. Skipping the data.";
                    continue;
                }

                // Check if the message is force or torque components
                int messageType = extractedMsgBottle.get(8 * subMsgIndex + 7).asFloat64();

                // Populate the serial message buffer
                if (messageType == 1) { //force

                    pImpl->serialPortWrenchDataVector.at(wrenchSourceNumber-1).forceBuffer.at(0) = xcomponent;
                    pImpl->serialPortWrenchDataVector.at(wrenchSourceNumber-1).forceBuffer.at(1) = ycomponent;
                    pImpl->serialPortWrenchDataVector.at(wrenchSourceNumber-1).forceBuffer.at(2) = zcomponent;

                    pImpl->serialPortWrenchDataVector.at(wrenchSourceNumber-1).forceUpdateFlag = true;
                }
                else if (messageType == 2) { //torque

                    pImpl->serialPortWrenchDataVector.at(wrenchSourceNumber-1).torqueBuffer.at(0) = xcomponent;
                    pImpl->serialPortWrenchDataVector.at(wrenchSourceNumber-1).torqueBuffer.at(1) = ycomponent;
                    pImpl->serialPortWrenchDataVector.at(wrenchSourceNumber-1).torqueBuffer.at(2) = zcomponent;

                    pImpl->serialPortWrenchDataVector.at(wrenchSourceNumber-1).torqueUpdateFlag = true;
                }

            }
        }

    }
    else {
        //yInfo() << LogPrefix << "Waiting for message on the serial port " << pImpl->serialComPortName;
    }


    for(size_t i = 0; i < pImpl->numberOfFTSensors; i++) {

        std::lock_guard<std::mutex> lock(pImpl->mutex);

        // Expose the data to IAnalogSensor when the complete message for a wrench source is updated
        if (pImpl->serialPortWrenchDataVector.at(i).forceUpdateFlag && pImpl->serialPortWrenchDataVector.at(i).torqueUpdateFlag) {

            // Expose the data as IAnalogSensor
            // ================================
            {
                pImpl->analogSensorData.measurements[6 * i + 0] = pImpl->serialPortWrenchDataVector.at(i).forceBuffer.at(0);
                pImpl->analogSensorData.measurements[6 * i + 1] = pImpl->serialPortWrenchDataVector.at(i).forceBuffer.at(1);
                pImpl->analogSensorData.measurements[6 * i + 2] = pImpl->serialPortWrenchDataVector.at(i).forceBuffer.at(2);
                pImpl->analogSensorData.measurements[6 * i + 3] = pImpl->serialPortWrenchDataVector.at(i).torqueBuffer.at(0);
                pImpl->analogSensorData.measurements[6 * i + 4] = pImpl->serialPortWrenchDataVector.at(i).torqueBuffer.at(1);
                pImpl->analogSensorData.measurements[6 * i + 5] = pImpl->serialPortWrenchDataVector.at(i).torqueBuffer.at(2);

                // Clear the serial message buffer after updating the IAnalogSensor buffers
                pImpl->serialPortWrenchDataVector.at(i).forceBuffer.clear();
                pImpl->serialPortWrenchDataVector.at(i).torqueBuffer.clear();
                pImpl->serialPortWrenchDataVector.at(i).forceBuffer.resize(3, 0.0);
                pImpl->serialPortWrenchDataVector.at(i).torqueBuffer.resize(3, 0.0);
                pImpl->serialPortWrenchDataVector.at(i).forceUpdateFlag = false;
                pImpl->serialPortWrenchDataVector.at(i).torqueUpdateFlag = false;
            }
        }

    }
}

bool ftnodeDriver::close()
{
    detach();
    return true;
}

bool ftnodeDriver::attach(yarp::dev::PolyDriver* poly)
{
    if (!poly) {
        yError() << LogPrefix << "Passed PolyDriver is a nullptr";
        return false;
    }

    if (pImpl->iSerialDevice || !poly->view(pImpl->iSerialDevice) || !pImpl->iSerialDevice) {
        yError() << LogPrefix << "Failed to view the ISerialDevice interface from the attached polydriver device";
        return false;
    }
    else {
        yInfo() << LogPrefix << "ISerialDevice interface viewed correctly";
    }

    // Get the comport name of the serial device
    pImpl->serialComPortName = poly->getValue("comport").asString();

    // TODO: Check if the ISerialDevice interface is configured correctly
    // I do not see any method to check this

    // Start the PeriodicThread loop
    if (!start()) {
        yError() << LogPrefix << "Failed to start the period thread.";
        return false;
    }

    yInfo() << LogPrefix << "attach() successful";
    return true;

}

bool ftnodeDriver::detach()
{
    while(yarp::os::PeriodicThread::isRunning()) {
        yarp::os::PeriodicThread::stop();
    }

    pImpl->iSerialDevice = nullptr;
    return true;
}

bool ftnodeDriver::attachAll(const yarp::dev::PolyDriverList& driverList)
{
    // A single serial device will be streaming data from all the sensors from the FTShoes
    if (driverList.size() > 1) {
        yError() << LogPrefix << "This wrapper accepts only one attached yarp Serial device";
        return false;
    }

    const yarp::dev::PolyDriverDescriptor* driver = driverList[0];

    if (!driver) {
        yError() << LogPrefix << "Passed PolyDriverDescriptor is nullptr";
        return false;
    }

    return attach(driver->poly);
}

bool ftnodeDriver::detachAll()
{
    return detach();
}

void ftnodeDriver::threadRelease()
{}

// =============
// IAnalogSensor
// =============

int ftnodeDriver::read(yarp::sig::Vector& out)
{
    out.resize(pImpl->analogSensorData.measurements.size());

    {
        std::lock_guard<std::mutex> lock(pImpl->mutex);
        std::copy(pImpl->analogSensorData.measurements.begin(),
                  pImpl->analogSensorData.measurements.end(),
                  out.data());
    }

    return IAnalogSensor::AS_OK;
}

int ftnodeDriver::getState(int ch)
{
    // Check if channel is in the right range
    if (ch < 0 || ch > pImpl->analogSensorData.numberOfChannels) {
        yError() << LogPrefix << "Failed to get status for channel" << ch;
        yError() << LogPrefix << "Channels must be in the range 0 -"
                 << pImpl->analogSensorData.numberOfChannels;
        return IAnalogSensor::AS_ERROR;
    }

    // TODO: Check if we can handle this better according to the serial port data
    return  IAnalogSensor::AS_OK;
}

int ftnodeDriver::getChannels()
{
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    return pImpl->analogSensorData.numberOfChannels;
}

int ftnodeDriver::calibrateSensor()
{
    // Not yet implemented
    return IAnalogSensor::AS_ERROR;
}

int ftnodeDriver::calibrateSensor(const yarp::sig::Vector& /*value*/)
{
    // Not yet implemented
    return IAnalogSensor::AS_ERROR;
}

int ftnodeDriver::calibrateChannel(int /*ch*/)
{
    // Not yet implemented
    return IAnalogSensor::AS_ERROR;
}

int ftnodeDriver::calibrateChannel(int /*ch*/, double /*value*/)
{
    // Not yet implemented
    return IAnalogSensor::AS_ERROR;
}
