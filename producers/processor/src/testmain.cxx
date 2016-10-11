
#include"Processor.hh"
// #include"ProcessorManager.hh"
#include"RawDataEvent.hh"


#include <chrono>
#include <thread>
#include <iostream>


using namespace eudaq;

int main(int argn, char **argc){
  PSSP p1 = Processor::MakeShared("TimeTriggerPS", "SYS:PSID=1");
  PSSP p2 = Processor::MakeShared("DummyDevicePS", "SYS:PSID=2");
  
  p1
    >>"EV(ADD=TRIGGER)"
    >>p2
    >>"EV(ADD=DUMMYDEV)"
    >>"EV(ADD=TRIGGER)"
    >>"SyncByTimestampPS(SYS:PSID=11)"
    // >>"ExamplePS(SYS:PSID=11)"
    // >>"EV(ADD=DUMMYDEV)"
    // >>"ExamplePS(SYS:PSID=12)"
    ;
  p2.reset();  
  p1<<"SYS:HB:RUN";
  p1<<"SYS:PD:RUN";
  
  std::cout << "press any key to exit...\n"; getchar();
  p1.reset();
}

// "EventFileReaderPS(SYS:PSID=0;FILE=/opt/eudaq/run000703.raw;SYS:SLEEP=1000;SYS:PD:RUN)"
// "EventReceiverPS(SYS:PSID=7;SETSERVER=tcp://40000;SYS:PD:RUN)"
// "EventSenderPS(SYS:PSID=6;CONNECT=Producer,p6,tcp://127.0.0.1:40000)"
// "ExamplePS(SYS:PSID=2222)"
// "EV(ADD=_DET)"

// std::cout<<"xxxxxxx"<<std::endl;
// {uint32_t i; std::cin>>i;}

// >>"DetEventUnpackInsertTimestampPS(SYS:PSID=100)"
// >>"EV(ADD=_RAW)"
// >>"SyncByTimestampPS(SYS:PSID=101)"
// >>"ExamplePS(SYS:PSID=11)"
// >>"EV(ADD=_RAW)"
// >>"ExamplePS(SYS:PSID=12)"
// >>"EventSenderPS(SYS:PSID=6;CONNECT=Producer,p6,tcp://127.0.0.1:40000)"

  
// PSSP p1 = Processor::MakePSSP("EventFileReaderPS", "SYS:PSID=1;FILE=../data/run000703.raw;SYS:SLEEP=1000");  

// PSSP p1 = Processor::MakePSSP("TimeTriggerPS", "SYS:PSID=1");
// PSSP p2 = Processor::MakePSSP("DummyDevicePS", "SYS:PSID=2");

// PSSP p3 = Processor::MakePSSP("ExamplePS", "SYS:PSID=3");
