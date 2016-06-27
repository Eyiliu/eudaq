#include"EventFileReaderPS.hh"


using namespace eudaq;

namespace{
  static RegisterDerived<Processor, typename std::string, EventFileReaderPS, uint32_t> reg_EXAMPLEPS("EventFileReaderPS");
}


EventFileReaderPS::EventFileReaderPS(uint32_t psid)
  :Processor("EventFileReaderPS", psid){
  InsertEventType(Event::str2id("_RAW"));
}

void EventFileReaderPS::OpenFile(std::string filename){
  m_des.reset(new FileDeserializer(filename));
  
}


void EventFileReaderPS::ProcessUserEvent(EVUP ev){
  std::cout<<">>>>PSID="<<GetID()<<"  PSType="<<GetType()<<"  EVType="<<ev->GetSubType()<<"  EVNum="<<ev->GetEventNumber()<<std::endl;
  ForwardEvent(std::move(ev));
}


void EventFileReaderPS::ProduceEvent(){
  if (!m_des) EUDAQ_THROW("m_des is not created!");
  while(1){
    EVUP ev(EventFactory::Create(*m_des.get()));//TODO: check if next event
    ProcessUserEvent(std::move(ev));
    // Processing(std::move(ev));
  }
}