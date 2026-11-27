#include "dfs/service/storage_node_rpc.hpp"
#include <charconv>
#include <iostream>
#include <utility>
#include <string>
#include <vector>
namespace {
void usage(){std::cerr<<"Usage: dfs_repair_cli audit|repair CHUNK_ID REPLICATION_FACTOR NODE_ID HOST PORT [NODE_ID HOST PORT ...]\n";}
dfs::Result<std::vector<dfs::StorageNodeEndpoint>> parse_nodes(int argc,char* argv[],int start){
 if((argc-start)<3||((argc-start)%3)!=0)return dfs::Result<std::vector<dfs::StorageNodeEndpoint>>::failure("endpoints must be NODE_ID HOST PORT triples");
 std::vector<dfs::StorageNodeEndpoint> nodes;
 for(int i=start;i<argc;i+=3){unsigned port=0;const std::string text(argv[i+2]);const auto parsed=std::from_chars(text.data(),text.data()+text.size(),port);
 if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||port==0U||port>65535U)return dfs::Result<std::vector<dfs::StorageNodeEndpoint>>::failure("invalid endpoint port");
 nodes.push_back({argv[i],argv[i+1],static_cast<std::uint16_t>(port)});}
 return dfs::Result<std::vector<dfs::StorageNodeEndpoint>>::success(std::move(nodes));}
const char* health_name(dfs::ReplicaHealth h){switch(h){case dfs::ReplicaHealth::healthy:return "healthy";case dfs::ReplicaHealth::missing:return "missing";case dfs::ReplicaHealth::unavailable:return "unavailable";case dfs::ReplicaHealth::corrupt:return "corrupt";case dfs::ReplicaHealth::divergent:return "divergent";case dfs::ReplicaHealth::repair_failed:return "repair-failed";}return "unknown";}
void print_report(const dfs::ReplicaRepairReport& r){std::cout<<"chunk="<<r.chunk_id<<" desired="<<r.desired_replica_count<<" source="<<(r.source_node_id.empty()?"none":r.source_node_id)<<" complete="<<(r.complete()?"yes":"no")<<'\n';for(const auto& o:r.observations)std::cout<<"  "<<o.node_id<<" "<<health_name(o.health)<<": "<<o.detail<<'\n';if(!r.repaired_node_ids.empty()){std::cout<<"repaired:";for(const auto& id:r.repaired_node_ids)std::cout<<" "<<id;std::cout<<'\n';}if(!r.unresolved_node_ids.empty()){std::cout<<"unresolved:";for(const auto& id:r.unresolved_node_ids)std::cout<<" "<<id;std::cout<<'\n';}}
}
int main(int argc,char* argv[]){if(argc<7){usage();return 2;}const std::string command(argv[1]),chunk_id(argv[2]);if(command!="audit"&&command!="repair"){usage();return 2;}unsigned factor=0;const std::string text(argv[3]);const auto parsed=std::from_chars(text.data(),text.data()+text.size(),factor);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||factor==0U){usage();return 2;}const auto nodes=parse_nodes(argc,argv,4);if(!nodes){std::cerr<<nodes.error()<<'\n';usage();return 2;}dfs::ReplicaRepairService service(nodes.value(),factor);const auto report=command=="audit"?service.audit_chunk(chunk_id):service.repair_chunk(chunk_id);if(!report){std::cerr<<report.error()<<'\n';return 1;}print_report(report.value());return report.value().complete()?0:1;}
