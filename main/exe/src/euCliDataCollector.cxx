#include "eudaq/OptionParser.hh"
#include "eudaq/DataCollector.hh"
#include <iostream>

int main(int /*argc*/, const char **argv) {
  eudaq::OptionParser op("EUDAQ Command Line DataCollector", "2.0", "The datacollector lauhcher of EUDAQ");
  eudaq::Option<std::string> name(op, "n", "name", "", "string",
				  "The eudaq application to be launched");  
  eudaq::Option<std::string> tname(op, "t", "tname", "", "string",
				  "Runtime name of the eudaq application");
  eudaq::Option<std::string> listen(op, "a", "listen-port", "", "address",
				  "The listenning port this ");
  eudaq::Option<std::string> rctrl(op, "r", "runcontrol", "tcp://localhost:44000", "address",
  				   "The address of the RunControl to connect");
  op.Parse(argv);
  std::string app_name = name.Value();
  if(app_name.find("DataCollector") != std::string::npos){
    auto app=eudaq::Factory<eudaq::DataCollector>::MakeShared<const std::string&,const std::string&>
      (eudaq::str2hash(name.Value()), tname.Value(), rctrl.Value());
    if(!app){
      std::cout<<"unknow DataCollector"<<std::endl;
      return -1;
    }
    app->SetServerAddress(listen.Value());
    app->Exec();
  }
  else{
    std::cout<<"unknow application"<<std::endl;
    return -1;
  }
  return 0;
}
