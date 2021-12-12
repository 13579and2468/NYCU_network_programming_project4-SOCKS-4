#include <cstdlib>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <sstream>
#include <regex>

using boost::asio::ip::tcp;
namespace bp = boost::process;
using bp::posix::fd;
using namespace std;
boost::asio::io_context io_context_init;
//boost::asio::io_context io_context_after;


int fork_until_success(){
  int r;
  while( (r=fork())==-1 );
  return r;
}

bool firewall_check(char thecmd,string dst_ip){
  ifstream ifs( "socks.conf" , ifstream::in ); 
  string permit,cmd,target;
  while(ifs>>permit>>cmd>>target)
  {
    if(!(cmd=="c" && thecmd == '\x01')  && !(cmd=="b" && thecmd == '\x02'))continue;
    boost::replace_all(target, ".", "\\.");
    boost::replace_all(target, "*", "[0-9]{1,3}");
    if (regex_match(dst_ip, regex(target))) return true;
  }
  return false;
}

class session
  : public std::enable_shared_from_this<session>
{
public:

  session(tcp::socket socket)
    : client_side_socket(std::move(socket)),server_side_socket(io_context_init)
  {
    count=0;
    socks4a_after_null = false;
    accept = true;
  }

  void start()
  {
    do_read_request();
  }

private:
  void do_read_request()
  {
    auto self(shared_from_this());
    client_side_socket.async_read_some(boost::asio::buffer(onebyte),
        [this, self](boost::system::error_code ec, std::size_t bytes_transferred)
        {
            if (!ec)
            {
              count++;
              strbuf += onebyte[0];
              if(count<9 || onebyte[0]!='\x00')
              {
                do_read_request();
                return;
              }
              if(int(strbuf[4])==0&&int(strbuf[5])==0&&int(strbuf[6])==0)
              {
                if(!socks4a_after_null)
                {
                  FirstNullPlace = count;
                  socks4a_after_null = true;
                  do_read_request();
                  return;
                }
              }
              cout<<"<S_IP>: "<< client_side_socket.remote_endpoint().address().to_string()<<endl;
              cout<<"<S_PORT>: "<<client_side_socket.remote_endpoint().port()<<endl;
              if(socks4a_after_null)
              {
                hostent *he = gethostbyname (strbuf.substr(FirstNullPlace).c_str());
                if(!he)
                {
                  cerr<<"cannot resolve domain name : "+strbuf.substr(FirstNullPlace)<<endl;
                }
                cout<<"<D_IP>: "<<inet_ntoa (*((struct in_addr *) he->h_addr_list[0]))<<endl;
                server_addr = string(inet_ntoa (*((struct in_addr *) he->h_addr_list[0])));
              }else{
                cout<<"<D_IP>: "<<int((unsigned char)strbuf[4])<<":"<<int((unsigned char)strbuf[5])<<":"<<int((unsigned char)strbuf[6])<<":"<<int((unsigned char)strbuf[7])<<endl;
                server_addr = to_string(int((unsigned char)strbuf[4]))+":"+to_string(int((unsigned char)strbuf[5]))+":"+to_string(int((unsigned char)strbuf[6]))+":"+to_string(int((unsigned char)strbuf[7]));
              }
              cout<<"<D_PORT>: "<<(unsigned char)(strbuf[2])*256+(unsigned char)strbuf[3]<<endl;
              server_port = (unsigned char)(strbuf[2])*256+(unsigned char)strbuf[3];
              if(strbuf[1]=='\x01')cout<<"<Command>: "<<"CONNECT\n";
              if(strbuf[1]=='\x02')cout<<"<Command>: "<<"BIND\n";
              if(firewall_check(strbuf[1],server_addr))
              {
                accept = true;
                cout<<"<Reply>: "<<"Accept"<<endl;
              }else{
                accept = false;
                cout<<"<Reply>: "<<"Reject"<<endl;
              }
              do_accept_request(int((unsigned char)strbuf[1]));
            }
        });
  }

  void do_accept_request(int cd)
  {
    auto self(shared_from_this());
    if(cd == 1) //connect to service
    {
      tcp::endpoint ep( boost::asio::ip::address::from_string(server_addr), server_port);
      server_side_socket.open(tcp::v4());
      server_side_socket.connect(ep);
      if(accept)
      {
        boost::asio::write(client_side_socket,boost::asio::buffer("\x00\x5a\x00\x00\x00\x00\x00\x00",8));
      }else
      {
        boost::asio::write(client_side_socket,boost::asio::buffer("\x00\x5b\x00\x00\x00\x00\x00\x00",8));
      }
    }

    do_client_read();
    do_server_read();
  }

  void do_client_read()
  {
    auto self(shared_from_this());
    client_side_socket.async_read_some(boost::asio::buffer(clientonebyte),
        [this, self](boost::system::error_code ec, std::size_t bytes_transferred)
        {
          if ((boost::asio::error::eof == ec) || (boost::asio::error::connection_reset == ec))
          {
            exit(0);
          }
          if (!ec)
          {
            boost::asio::write(server_side_socket,boost::asio::buffer(clientonebyte,1));
          }
          do_client_read();
        });
  }

  void do_server_read()
  {
    auto self(shared_from_this());
    server_side_socket.async_read_some(boost::asio::buffer(serveronebyte),
        [this, self](boost::system::error_code ec, std::size_t bytes_transferred)
        {
          if ((boost::asio::error::eof == ec) || (boost::asio::error::connection_reset == ec))
          {
            exit(0);
          }
          if (!ec)
          {
            boost::asio::write(client_side_socket,boost::asio::buffer(serveronebyte,1));
          }
          do_server_read();
        });
  }

  string server_addr;
  uint server_port;
  int FirstNullPlace;
  bool socks4a_after_null;
  int count;
  string strbuf;
  std::array<char, 1> onebyte;
  std::array<char, 1> clientonebyte;
  std::array<char, 1> serveronebyte;
  tcp::socket client_side_socket;
  tcp::socket server_side_socket;
  bool accept;
};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    do_accept();
  }

private:
  void do_accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
            io_context_init.notify_fork(boost::asio::io_context::fork_prepare);
            int pid = fork_until_success();
            if (!ec && pid==0)
            {
              io_context_init.notify_fork(boost::asio::io_context::fork_child);
              std::make_shared<session>(std::move(socket))->start();
            }
            io_context_init.notify_fork(boost::asio::io_context::fork_parent);
            do_accept();
        });
  }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[])
{
    //ignoring SIGCHILD will give zombie to init process
  signal(SIGCHLD,SIG_IGN);
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: socks_server <port>\n";
      return 1;
    }

    server s(io_context_init, std::atoi(argv[1]));

    io_context_init.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}