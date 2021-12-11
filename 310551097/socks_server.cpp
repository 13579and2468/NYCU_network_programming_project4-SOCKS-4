#include <cstdlib>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <sstream>

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
              cerr<<"g";
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
                cout<<"<D_IP>: "<<int(strbuf[4])<<":"<<int(strbuf[5])<<":"<<int(strbuf[6])<<":"<<int(strbuf[7])<<endl;
                server_addr = to_string(int(strbuf[4]))+":"+to_string(int(strbuf[5]))+":"+to_string(int(strbuf[6]))+":"+to_string(int(strbuf[7]));
              }
              cout<<"<D_PORT>: "<<strbuf[2]*256+strbuf[3]<<endl;
              server_port = strbuf[2]*256+strbuf[3];
              if(strbuf[1]=='\x01')cout<<"<Command>: "<<"CONNECT\n";
              if(strbuf[1]=='\x02')cout<<"<Command>: "<<"BIND\n";
              cout<<"<Reply>: "<<"Accept"<<endl;
              do_accept_request(int(strbuf[1]));
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
          cerr<<"aaa";
          if (!ec)
          {
            boost::asio::write(server_side_socket,boost::asio::buffer(clientonebyte,1));
          }
          do_client_read();
        });
      cerr<<"ccc";
  }

  void do_server_read()
  {
    cerr<<"ddd";
    auto self(shared_from_this());
    server_side_socket.async_read_some(boost::asio::buffer(serveronebyte),
        [this, self](boost::system::error_code ec, std::size_t bytes_transferred)
        {
          cerr<<"bbb";
          if (!ec)
          {
            boost::asio::write(client_side_socket,boost::asio::buffer(serveronebyte,1));
          }
          do_server_read();
        });
  }

  string server_addr;
  int server_port;
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
            //int pid = fork_until_success();
            //if (!ec && pid==0)
            //{
                //acceptor_.close(ec);
                std::make_shared<session>(std::move(socket))->start();
                //io_context_init.run();
            //}
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