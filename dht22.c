#include <signal.h>

#include "open62541.h"
#include "pi_dht_read.h"

#define SENSOR_TYPE     DHT22
#define SENSOR_GPIO_PIN 4

#define READ_TEMPERATURE    1
#define READ_HUMIDITY       2

UA_Boolean running = true;
UA_Logger logger = UA_Log_Stdout;

typedef struct Readings {
    UA_DateTime readTime;
    UA_Float    humidity;
    UA_Float    temperature;
    UA_Int32    status;
} Readings;
Readings readings = {.readTime = 0, .humidity = 0.0f, .temperature = 0.0f, .status=DHT_ERROR_GPIO};

static void stopHandler(int sign) {
    UA_LOG_INFO(logger, UA_LOGCATEGORY_SERVER, "received ctrl-c");
    running = false;
}

static void readSensorJob(UA_Server *server, void *data) {
    readings.status = pi_dht_read(SENSOR_TYPE, SENSOR_GPIO_PIN, &readings.humidity, &readings.temperature);
    readings.readTime = UA_DateTime_now();
}

static UA_StatusCode
readSensor(void *handle, const UA_NodeId nodeid, UA_Boolean sourceTimeStamp,
            const UA_NumericRange *range, UA_DataValue *dataValue) {
    dataValue->hasValue = true;
    if((UA_Int32)handle == READ_TEMPERATURE){
        UA_Variant_setScalarCopy(&dataValue->value, &readings.temperature, &UA_TYPES[UA_TYPES_FLOAT]);
    } else {
        UA_Variant_setScalarCopy(&dataValue->value, &readings.humidity, &UA_TYPES[UA_TYPES_FLOAT]);
    }
    dataValue->hasSourceTimestamp = true;
    UA_DateTime_copy(&readings.readTime, &dataValue->sourceTimestamp);
    dataValue->hasStatus = true;
    switch(readings.status){
        case DHT_SUCCESS:
            dataValue->status = UA_STATUSCODE_GOOD;
            break;
        case DHT_ERROR_TIMEOUT:
            dataValue->status = UA_STATUSCODE_BADTIMEOUT;
            break;
        case DHT_ERROR_GPIO:
            dataValue->status = UA_STATUSCODE_BADCOMMUNICATIONERROR;
            break;
        default:
            dataValue->status = UA_STATUSCODE_BADNOTFOUND;
            break;
    }
    return UA_STATUSCODE_GOOD;
}

int main(int argc, char** argv) {
    signal(SIGINT, stopHandler); /* catches ctrl-c */

    UA_ServerConfig config = UA_ServerConfig_standard;
    UA_ServerNetworkLayer nl = UA_ServerNetworkLayerTCP(UA_ConnectionConfig_standard, 16664);
    config.networkLayers = &nl;
    config.networkLayersSize = 1;
    UA_Server *server = UA_Server_new(config);

    /* add a sensor sampling job to the server */
    UA_Job job = {.type = UA_JOBTYPE_METHODCALL,
                  .job.methodCall = {.method = readSensorJob, .data = NULL} };
    UA_Server_addRepeatedJob(server, job, 2500, NULL);


    /* adding temperature node */
    UA_NodeId temperatureNodeId = UA_NODEID_STRING(1, "temperature");
    UA_QualifiedName temperatureNodeName = UA_QUALIFIEDNAME(1, "temperature");
    UA_DataSource temperatureDataSource = (UA_DataSource) {
        .handle = (void*)READ_TEMPERATURE, .read = readSensor, .write = NULL};
    UA_VariableAttributes temperatureAttr;
    UA_VariableAttributes_init(&temperatureAttr);
    temperatureAttr.description = UA_LOCALIZEDTEXT("en_US","temperature");
    temperatureAttr.displayName = UA_LOCALIZEDTEXT("en_US","temperature");

    UA_Server_addDataSourceVariableNode(server, temperatureNodeId,
                                        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                                        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                        temperatureNodeName, UA_NODEID_NULL, temperatureAttr, temperatureDataSource, NULL);

    /* adding humidity node */
    UA_NodeId humidityNodeId = UA_NODEID_STRING(1, "humidity");
    UA_QualifiedName humidityNodeName = UA_QUALIFIEDNAME(1, "humidity");
    UA_DataSource humidityDataSource = (UA_DataSource) {
        .handle = (void*)READ_HUMIDITY, .read = readSensor, .write = NULL};
    UA_VariableAttributes humidityAttr;
    UA_VariableAttributes_init(&humidityAttr);
    humidityAttr.description = UA_LOCALIZEDTEXT("en_US","humidity");
    humidityAttr.displayName = UA_LOCALIZEDTEXT("en_US","humidity");

    UA_Server_addDataSourceVariableNode(server, humidityNodeId,
                                        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                                        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                        humidityNodeName, UA_NODEID_NULL, humidityAttr, humidityDataSource, NULL);

    UA_StatusCode retval = UA_Server_run(server, &running);
    UA_Server_delete(server);
    nl.deleteMembers(&nl);
    return (int)retval;
}
