#include "DataCollector.hh"
#include "TransportFactory.hh"
#include "BufferSerializer.hh"
#include "DetectorEvent.hh"
#include "Logger.hh"
#include "Utils.hh"
#include "Processor.hh"
#include "TLUEvent.hh"

#include <iostream>
#include <ostream>

namespace eudaq {

  DataCollector::DataCollector(const std::string &name,
                               const std::string &runcontrol,
                               const std::string &listenaddress,
                               const std::string &runnumberfile)
      : CommandReceiver("DataCollector", name, runcontrol, false),
        m_runnumberfile(runnumberfile), m_done(false), m_listening(true),
        m_numwaiting(0), m_itlu((size_t)-1),
        m_runnumber(ReadFromFile(runnumberfile, 0U)), m_eventnumber(0),
        m_runstart(0) {
    
    m_dataserver.reset(TransportFactory::CreateServer(listenaddress));
    m_dataserver->SetCallback(TransportCallback(this, &DataCollector::DataHandler));
    EUDAQ_DEBUG("Instantiated datacollector with name: " + name);
    m_thread = std::unique_ptr<std::thread>(new std::thread(&DataCollector::DataThread, this));
    EUDAQ_DEBUG("Listen address=" + to_string(m_dataserver->ConnectionString()));
    CommandReceiver::StartThread();
  }

  DataCollector::~DataCollector() {
    m_done = true;
    m_thread->join();
  }

  void DataCollector::OnServer() {
    m_status.SetTag("_SERVER", m_dataserver->ConnectionString());
  }

  void DataCollector::OnGetRun() {
    m_status.SetTag("_RUN", to_string(m_runnumber));
  }

  void DataCollector::OnConnect(const ConnectionInfo &id) {
    EUDAQ_INFO("Connection from " + to_string(id));
    Info info;
    info.id = std::shared_ptr<ConnectionInfo>(id.Clone());
    m_buffer.push_back(info);
    if (id.GetType() == "Producer" && id.GetName() == "TLU") {
      m_itlu = m_buffer.size() - 1;
    }
  }

  void DataCollector::OnDisconnect(const ConnectionInfo &id) {
    EUDAQ_INFO("Disconnected: " + to_string(id));
    
    size_t i = GetInfo(id);
    if (i == m_itlu) {
      m_itlu = (size_t)-1;
    } else if (i < m_itlu) {
      --m_itlu;
    }

    m_buffer.erase(m_buffer.begin() + i);
  }

  void DataCollector::OnConfigure(const Configuration &param) {
    m_config = param;
    std::string fwtype = m_config.Get("FileType", "native");
    std::string fwpatt = m_config.Get("FilePattern", "run$6R_tp$X");
    std::string sync = m_config.Get("SyncMethod", "runnumber");

    m_ps_col.clear();
    m_ps_input.reset();
    std::cout<<"....................OnConfig.....\n";
    std::cout<< "SyncMethod="<<sync<<std::endl;
    if(sync == "timestamp"){
      std::string config_key = "PS_CHAIN";
      std::string config_val;
      uint32_t n = 0;
      while(1){
	config_val = m_config.Get(config_key+std::to_string(n), "");
	if(config_val.empty())
	  break;
	std::cout<<config_key+std::to_string(n)<<"="<<config_val<<std::endl;

	std::stringstream ss(config_val);
	std::string item;
	std::vector<std::string> elems;
	while (std::getline(ss, item, ':')) {
	  item=trim(item);
	  elems.push_back(item);
	}
	if(elems[2]=="CREATE"){
	  uint32_t ps_n = std::stoul(elems[1]); 
	  std::string cmd("SYS:PSID");
	  cmd+=elems[1];
	  m_ps_col[ps_n] = Processor::MakeShared(elems[3], cmd);
	}
	if(elems[2]=="CMD"){
	  uint32_t ps_n = std::stoul(elems[1]);
	  std::string cmd = elems[3];
	  for(uint32_t i = 4; i< elems.size(); i++)
	    cmd = cmd + ":" + elems[i];
	  std::cout<< "cmd ="<<cmd<<"\n";
	  m_ps_col[ps_n]->ProcessCmd(cmd);
	}
	if(elems[2]=="PIPE"){
	  uint32_t ps_n = std::stoul(elems[1]);
	  uint32_t ps_m = std::stoul(elems.back());
	  std::stringstream evss(elems[3]);
	  std::string evtype;
	  while (std::getline(evss, evtype, '+')) {
	    evtype=trim(evtype);
	    m_ps_col[ps_n]+evtype;
	  }
	  m_ps_col[ps_n]>>m_ps_col[ps_m];
	}
	if(elems[2]=="INPUT"){
	  uint32_t ps_n = std::stoul(elems[1]);
	  m_ps_input = m_ps_col[ps_n];
	}
	n++;
      }
    }

    for(auto &e: m_ps_col){
      e.second->Print(std::cout);
    }
    
    uint32_t fwid = cstr2hash(fwtype.c_str());
    if(fwtype!="processor"||!m_writer){
      m_writer = Factory<FileWriter>::Create<std::string&>(fwid, fwpatt);
    }
  }

  void DataCollector::OnPrepareRun(unsigned runnumber) {
    EUDAQ_INFO("Preparing for run " + to_string(runnumber));
    m_runstart = Time::Current();
    try {
      if (!m_writer) {
        EUDAQ_THROW("You must configure before starting a run");
      }
      m_writer->StartRun(runnumber);
      WriteToFile(m_runnumberfile, runnumber);
      m_runnumber = runnumber;
      m_eventnumber = 0;

      for (size_t i = 0; i < m_buffer.size(); ++i) {
        if (m_buffer[i].events.size() > 0) {
          EUDAQ_WARN("Buffer " + to_string(*m_buffer[i].id) + " has " +
                     to_string(m_buffer[i].events.size()) +
                     " events remaining.");
          m_buffer[i].events.clear();
        }
      }
      m_numwaiting = 0;

      SetStatus(Status::LVL_OK);
    } catch (const Exception &e) {
      std::string msg =
          "Error preparing for run " + to_string(runnumber) + ": " + e.what();
      EUDAQ_ERROR(msg);
      SetStatus(Status::LVL_ERROR, msg);
    }
  }

  void DataCollector::OnStopRun() {
    EUDAQ_INFO("End of run " + to_string(m_runnumber));
  }

  void DataCollector::OnReceive(const ConnectionInfo &id, EventSP ev) {
    if(m_ps_input){
      auto ev_stm = ev->Clone();
      //DO SOME CHECK
      m_ps_input<<=std::move(ev_stm);
    }
    
    Info &inf = m_buffer[GetInfo(id)];
    inf.events.push_back(ev);
    
    bool tmp = false;
    if (inf.events.size() == 1) {
      m_numwaiting++;
      if (m_numwaiting == m_buffer.size()) {
        tmp = true;
      }
    }

    if (tmp)
      OnCompleteEvent();
  }

  void DataCollector::OnStatus() {
    std::string evt;
    if (m_eventnumber > 0)
      evt = to_string(m_eventnumber - 1);
    m_status.SetTag("EVENT", evt);
    m_status.SetTag("RUN", to_string(m_runnumber));
    if (m_writer.get())
      m_status.SetTag("FILEBYTES", to_string(m_writer->FileBytes()));
  }

  void DataCollector::OnCompleteEvent() {
    bool more = true;
    bool found_bore = false;

    while (more) {
      if (m_eventnumber < 10 || m_eventnumber % 1000 == 0) {
        std::cout << "Complete Event: " << m_runnumber << "." << m_eventnumber
                  << std::endl;
      }
      unsigned n_run = m_runnumber, n_ev = m_eventnumber;
      uint64_t n_ts = (uint64_t)-1;
      if (m_itlu != (size_t)-1) {
        TLUEvent *ev =
            static_cast<TLUEvent *>(m_buffer[m_itlu].events.front().get());
        n_run = ev->GetRunNumber();
        n_ev = ev->GetEventNumber();
        n_ts = ev->GetTimestampBegin();
      }
      DetectorEvent ev(n_run, n_ev, n_ts);
      unsigned tluev = 0;
      for (size_t i = 0; i < m_buffer.size(); ++i) {
        if (m_buffer[i].events.front()->GetRunNumber() != m_runnumber) {
          EUDAQ_ERROR("Run number mismatch in event " +
                      to_string(ev.GetEventNumber()));
        }
        if ((m_buffer[i].events.front()->GetEventNumber() != m_eventnumber) &&
            (m_buffer[i].events.front()->GetEventNumber() !=
             m_eventnumber - 1)) {
          if (ev.GetEventNumber() % 1000 == 0) {
            // dhaas: added if-statement to filter out TLU event number 0, in
            // case of bad clocking out
            if (m_buffer[i].events.front()->GetEventNumber() != 0)
              EUDAQ_WARN(
                  "Event number mismatch > 1 in event " +
                  to_string(ev.GetEventNumber()) + " " +
                  to_string(m_buffer[i].events.front()->GetEventNumber()) +
                  " " + to_string(m_eventnumber));
            if (m_buffer[i].events.front()->GetEventNumber() == 0)
              EUDAQ_WARN("Event number mismatch > 1 in event " +
                         to_string(ev.GetEventNumber()));
          }
        }
        ev.AddEvent(m_buffer[i].events.front());
        m_buffer[i].events.pop_front();
        if (m_buffer[i].events.size() == 0) {
          m_numwaiting--;
          more = false;
        }
      }
      if (ev.IsBORE()) {
        ev.SetTag("STARTTIME", m_runstart.Formatted());
        ev.SetTag("CONFIG", to_string(m_config));
        found_bore = true;
      }
      if (ev.IsEORE()) {
        ev.SetTag("STOPTIME", Time::Current().Formatted());
        EUDAQ_INFO("Run " + to_string(ev.GetRunNumber()) + ", EORE = " +
                   to_string(ev.GetEventNumber()));
      }
      if (m_writer.get()) {
        try {
          m_writer->WriteEvent(ev);
        } catch (const Exception &e) {
          std::string msg = "Exception writing to file: ";
          msg += e.what();
          EUDAQ_ERROR(msg);
          SetStatus(Status::LVL_ERROR, msg);
        }
      } else {
        EUDAQ_ERROR("Event received before start of run");
      }

      // Only increase the internal event counter for non-BORE events.
      // This is required since all producers start sending data with event ID 0
      // but the data collector would already be at 1, since BORE was 0.
      if (!found_bore)
        ++m_eventnumber;
    }
  }

  size_t DataCollector::GetInfo(const ConnectionInfo &id) {
    for (size_t i = 0; i < m_buffer.size(); ++i) {
      if (m_buffer[i].id->Matches(id))
        return i;
    }
    EUDAQ_THROW("Unrecognised connection id");
  }

  void DataCollector::DataHandler(TransportEvent &ev) {
    switch (ev.etype) {
    case (TransportEvent::CONNECT):
      if (m_listening) {
        m_dataserver->SendPacket("OK EUDAQ DATA DataCollector", ev.id, true);
      } else {
        m_dataserver->SendPacket(
            "ERROR EUDAQ DATA Not accepting new connections", ev.id, true);
        m_dataserver->Close(ev.id);
      }
      break;
    case (TransportEvent::DISCONNECT):
      OnDisconnect(ev.id);
      break;
    case (TransportEvent::RECEIVE):
      if (ev.id.GetState() == 0) { // waiting for identification
        do {
          size_t i0 = 0, i1 = ev.packet.find(' ');
          if (i1 == std::string::npos)
            break;
          std::string part(ev.packet, i0, i1);
          if (part != "OK")
            break;
          i0 = i1 + 1;
          i1 = ev.packet.find(' ', i0);
          if (i1 == std::string::npos)
            break;
          part = std::string(ev.packet, i0, i1 - i0);
          if (part != "EUDAQ")
            break;
          i0 = i1 + 1;
          i1 = ev.packet.find(' ', i0);
          if (i1 == std::string::npos)
            break;
          part = std::string(ev.packet, i0, i1 - i0);
          if (part != "DATA")
            break;
          i0 = i1 + 1;
          i1 = ev.packet.find(' ', i0);
          part = std::string(ev.packet, i0, i1 - i0);
          ev.id.SetType(part);
          i0 = i1 + 1;
          i1 = ev.packet.find(' ', i0);
          part = std::string(ev.packet, i0, i1 - i0);
          ev.id.SetName(part);
        } while (false);
        m_dataserver->SendPacket("OK", ev.id, true);
        ev.id.SetState(1); // successfully identified
        OnConnect(ev.id);
      } else {
        BufferSerializer ser(ev.packet.begin(), ev.packet.end());
	uint32_t id;
	ser.PreRead(id);
	EventSP event = Factory<Event>::Create<Deserializer&>(id, ser);
	OnReceive(ev.id, event);
      }
      break;
    default:
      std::cout << "Unknown:    " << ev.id << std::endl;
    }
  }

  void DataCollector::DataThread() {
    try {
      while (!m_done) {
        m_dataserver->Process(100000);
      }
    } catch (const std::exception &e) {
      std::cout << "Error: Uncaught exception: " << e.what() << "\n"
                << "DataThread is dying..." << std::endl;
    } catch (...) {
      std::cout << "Error: Uncaught unrecognised exception: \n"
                << "DataThread is dying..." << std::endl;
    }
  }
}
