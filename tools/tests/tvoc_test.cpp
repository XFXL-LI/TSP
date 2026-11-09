// Production bodies are extracted by Run-TvocTests; only hardware dependencies are mocked.
#include "test_string.h"
#include <cmath>
#include <cJSON.h>
#include "module/tvoc/TvocPolicy.h"
#include "module/gas/GasUnitConverter.h"
#include "module/diagnostics/ReadDeadline.h"
#include "module/diagnostics/RealtimeSnapshotInfo.h"
#include "module/json/GetDataResponse.h"
#define VERSION2 "test"
#define LOG_INFO(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
constexpr int pdTRUE = 1;
#define pdMS_TO_TICKS(x) (x)
using TickType_t = uint32_t;
static uint32_t clockMs;
uint32_t millis() { return clockMs; }
void vTaskDelay(uint32_t ms) { clockMs += ms; }
uint64_t esp_timer_get_time() { return uint64_t(clockMs) * 1000; }
static unsigned locks;
static bool refuseLock;
int xSemaphoreTake(void*, unsigned) { if(refuseLock)return 0;++locks;return 1; }
void xSemaphoreGive(void*) { assert(locks);--locks; }
struct Stream {};
class SerialManager {
public:
 static SerialManager& getInstance(){static SerialManager s;return s;}
 void* getMutex(const char*){return reinterpret_cast<void*>(1);}
};
#define SERIAL_485 "485"
#include "production_types.h"
class BaseCollector {
public:
 virtual ~BaseCollector()=default;
 virtual bool begin()=0;
 virtual bool modbusInit(Stream*,String,String,float,String)=0;
 virtual bool gal(int,int)=0;
 virtual String getID()const=0;
 virtual DataPacket* collect()=0;
 virtual DataPacket* collect(ReadDeadline)=0;
};
static std::map<String,BaseCollector*> registry;
struct CollectorRegistrar { CollectorRegistrar(const char* id,BaseCollector* p){registry[id]=p;} };
static unsigned measurements,readCost=10;
static int badMeasurement=-1,badStatus=-1,singleGood=-1;
static bool wrongVersion;
static std::vector<uint32_t> measurementTimes;
static std::vector<uint16_t> addresses;
static uint32_t levels[12];
static uint16_t flags[12];
class modbus_manager {
public:
 void modbus_init(Stream*){}
 bool readModbusRegs(uint8_t slave,uint16_t addr,uint16_t count,uint16_t* out,uint8_t attempts,uint32_t timeout,uint32_t delay){
  assert(slave==3 && attempts==1 && timeout==500 && delay==0);
  addresses.push_back(addr);clockMs+=readCost;
  if(addr==256){assert(count==3);out[0]=1;out[1]=wrongVersion?1:2;out[2]=0;return true;}
  if(addr==5){assert(count==2);measurementTimes.push_back(clockMs-readCost);unsigned i=measurements++%12;
   out[0]=levels[i]>>16;out[1]=levels[i]&65535;return int(i)!=badMeasurement && (singleGood<0 || int(i)==singleGood);}
  assert(addr==272 && count==4);unsigned i=(measurements-1)%12;
  out[0]=flags[i];out[1]=5;out[2]=4;out[3]=0;return int(i)!=badStatus;
 }
};
#include "adapter_source.h"
struct COLLECTCONFIG{String id,name;int alarmLimit=0;String unit;};
using COLLECTMAP=std::map<String,COLLECTCONFIG>;
#include "config_source.h"
#include "stats_source.h"
namespace CalendarClock{bool isValidTimestamp(uint64_t t){return t>=20200101000000ULL;}}
class DataManager{
public:
 void* _lastRealDataMutex=reinterpret_cast<void*>(1);
 std::map<String,ProcessedDataPacket> _last_real_snapshot;
 uint64_t _last_real_timestamp=0,_last_real_sampled_us=0;
 uint32_t _last_real_seq=0;
 uint8_t _tvocSamples=0,_tvocValidSamples=0;
 uint16_t _tvocFlags=0;
 char _tvocStatus='N';
 void updateLatestSnapshot(const AllDataPacket*);
 bool readLatestValues(const char* const*,size_t,float*,uint64_t&,RealtimeSnapshotInfo*);
};
#include "snapshot_source.h"
static DataManager snapshots;
static unsigned published,alarms;
static float lastValue;
static char lastStatus;
static bool lastValid;
static uint8_t lastCount,lastValidCount;
enum class EventID{RAW_DATA_COLLECTED};
class EventBus{
public:
 static EventBus& getInstance(){static EventBus b;return b;}
 int getSubscriberCount(EventID){return 0;}
 void publish(EventID,AllDataPacket* p){
  ++published;assert(p->data_map.size()==1 && p->data_map.count(TvocPolicy::ID));
  auto point=p->data_map.at(TvocPolicy::ID);lastValue=point->value;lastValid=point->is_valid;
  lastStatus=dataStatusCode(point->status);lastCount=point->sample_count;lastValidCount=point->valid_sample_count;
  snapshots.updateLatestSnapshot(p);
 }
};
void publishCollectionAlarm(const String&){++alarms;}
uint64_t currentCollectionTimestamp(){return 20261028120000ULL;}
class collectorManager{
public:
 std::vector<BaseCollector*> _collectors;
 bool isTvocMode()const;
 void pollTvoc(uint32_t,bool);
};
#include "collection_source.h"
static void reset(){
 assert(locks==0);clockMs=0;measurements=0;readCost=10;wrongVersion=false;
 badMeasurement=badStatus=singleGood=-1;refuseLock=false;published=alarms=0;
 addresses.clear();measurementTimes.clear();
 for(unsigned i=0;i<12;++i){levels[i]=100;flags[i]=1;}
 static Stream port;assert(TvocCollect::getInstance().modbusInit(&port,TvocPolicy::ID,"485",1,"ppb"));
}
static void testProtocol(){
 reset();auto& a=TvocCollect::getInstance();
 assert(registry.count("a99054")==1 && !registry.count("a24035"));assert(!a.gal(0,100));
 levels[0]=0;auto p=a.collect(ReadDeadline::at(5000));assert(p->is_valid && p->value==0 && p->status==DataStatus::NORMAL);p->release();
 assert((addresses==std::vector<uint16_t>{256,5,272}));
 reset();flags[0]=1|32;levels[0]=20000;p=a.collect(ReadDeadline::at(5000));
 assert(p->is_valid && p->value==20000 && p->status==DataStatus::OVER_RANGE);p->release();
 for(uint16_t f:{uint16_t(0),uint16_t(1|4),uint16_t(1|8),uint16_t(1|16)}){
  reset();flags[0]=f;p=a.collect(ReadDeadline::at(5000));assert(!p->is_valid);p->release();}
 reset();levels[0]=UINT32_MAX;p=a.collect(ReadDeadline::at(5000));assert(!p->is_valid);p->release();
 reset();badStatus=0;p=a.collect(ReadDeadline::at(5000));assert(!p->is_valid && p->status==DataStatus::COMMUNICATION_FAILURE);p->release();
 reset();badMeasurement=0;p=a.collect(ReadDeadline::at(5000));assert(!p->is_valid && addresses.back()==272);p->release();
 reset();wrongVersion=true;p=a.collect(ReadDeadline::at(5000));assert(!p->is_valid && addresses.size()==1);p->release();
 reset();refuseLock=true;p=a.collect(ReadDeadline::at(5000));assert(!p->is_valid && addresses.empty());p->release();refuseLock=false;
 reset();p=a.collect(ReadDeadline::at(1000));assert(!p->is_valid && addresses.empty());p->release();
 std::puts("PASS actual adapter: read-only v1.2.0, word order, zero, flags/invalid code, fresh status, version/mutex/deadline guards");
}
static void testBatches(){
 reset();collectorManager m;m._collectors={&TvocCollect::getInstance()};assert(m.isTvocMode());
 levels[5]=1000;m.pollTvoc(0,true);assert(published==1 && measurements==12 && clockMs==70000);
 assert(lastValue==175 && lastCount==12 && lastValidCount==12 && lastValid);
 for(unsigned i=0;i<12;++i)assert(measurementTimes[i]>=10000+i*5000 && measurementTimes[i]<15000+i*5000);
 const char* ids[]={TvocPolicy::ID};float values[1];uint64_t ts;RealtimeSnapshotInfo sample;
 assert(snapshots.readLatestValues(ids,1,values,ts,&sample));
 assert(values[0]==175 && sample.tvocSamples==12 && sample.tvocValidSamples==12 && sample.validMask==1);
 char json[1024];size_t used;uint8_t dec[]={0};DeviceStatusSnapshot dev;
 assert(GetDataResponse::build(json,sizeof(json),used,ids,values,dec,1,ts,99,25,50,sample,&dev));
 assert(strstr(json,"\"tvoc\":{\"samples\":12,\"valid\":12,") && used+2<=800);
 assert(GetDataResponse::build(json,sizeof(json),used,ids,values,dec,1,ts,99,25,50,sample,nullptr));assert(!strstr(json,"\"tvoc\""));
 for(unsigned i=0;i<10;++i)assert(snapshots.readLatestValues(ids,1,values,ts,&sample));
 assert(published==1 && measurements==12 && sample.seq==snapshots._last_real_seq);
 reset();singleGood=6;levels[6]=0;m.pollTvoc(0,true);assert(lastValid && lastValue==0 && lastValidCount==1 && lastCount==12);
 reset();singleGood=99;m.pollTvoc(0,true);assert(!lastValid && lastValidCount==0 && lastStatus=='B');
 reset();flags[4]=1|32;levels[4]=20000;m.pollTvoc(0,true);assert(lastValid && lastStatus=='T' && lastValue>500);
 reset();m.pollTvoc(0,false);assert(!lastValid && measurements==0 && lastCount==12);
 reset();clockMs=24000;m.pollTvoc(0,true);assert(measurements==9 && measurementTimes.front()>=25000 && measurementTimes.front()<30000 && lastCount==12);
 reset();clockMs=UINT32_MAX-20000;uint32_t start=clockMs;m.pollTvoc(start,true);assert(measurements==12 && uint32_t(clockMs-start)==70000 && lastValid);
 reset();clockMs=73000;m.pollTvoc(0,true);assert(measurements==0 && !lastValid);
 TvocPolicy::Batch high;TvocPolicy::Sample s;s.valid=true;s.status='N';s.ppb=UINT32_MAX-1;
 for(unsigned i=0;i<12;++i)high.add(s);assert(high.sum==uint64_t(UINT32_MAX-1)*12 && high.validCount==12);
 std::puts("PASS actual collection/snapshot: 12 slots, one batch, spikes retained, one valid zero, failures, pump unavailable, late slots and clock wrap");
}
static void testConfigUnitsStats(){
 auto root=cJSON_Parse("{\"a24035\":{\"unit\":\"PPM\",\"name\":\"TVOC\",\"alarmLimit\":500}}");
 normalizeGasUnitsInJson(root);assert(validTvocSelection(root));assert(!cJSON_GetObjectItem(root,"a24035"));
 assert(!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(root,"a99054"),"unit")->valuestring,"ppm"));
 assert(sensorDefinitionIndex("a24035")==sensorDefinitionIndex("a99054"));
 cJSON_AddObjectToObject(root,"w34011");assert(!validTvocSelection(root));cJSON_Delete(root);
 root=cJSON_Parse("{\"a99054\":{\"unit\":\"bad\"}}");normalizeGasUnitsInJson(root);
 assert(!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(root,"a99054"),"unit")->valuestring,"ppb"));cJSON_Delete(root);
 assert(GasUnitConverter::isGasSensor(TvocPolicy::ID) && !GasUnitConverter::isK7sSensor(TvocPolicy::ID));
 assert(GasUnitConverter::isK7sSensor("w34011"));
 assert(std::fabs(GasUnitConverter::convertFromPpb(TvocPolicy::ID,1000,"ppm")-1)<1e-6);
 assert(std::fabs(GasUnitConverter::convertFromPpb(TvocPolicy::ID,1000,"mg/m3")-2.29329)<0.0001);
 assert(std::fabs(GasUnitConverter::convertFromPpb(TvocPolicy::ID,1000,"ug/m3")-2293.29)<0.1);
 assert(std::fabs(GasUnitConverter::convertFromPpb("w34011",100,"ug/m3")-196.319)<0.001);
 StatValue stat;stat.update(100);stat.update(200,DataStatus::OVER_RANGE);stat.observeInvalid(DataStatus::COMMUNICATION_FAILURE);
 assert(stat.getAvg()==150 && stat.getMin()==100 && stat.getMax()==200 && stat.getStatus()==DataStatus::OVER_RANGE);
 stat.reset();assert(stat.count==0);stat.update(0);assert(stat.getAvg()==0 && stat.getStatus()==DataStatus::NORMAL);
 std::puts("PASS actual config migration/exclusivity, four declared TVOC units, unchanged O3 conversion and aggregate status");
}
int main(){testProtocol();testBatches();testConfigUnitsStats();assert(locks==0);std::puts("All TVOC host regressions passed; hardware not verified.");}
