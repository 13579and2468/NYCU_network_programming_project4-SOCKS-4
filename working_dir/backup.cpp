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

int fork_until_success(){
  int r;
  while( (r=fork())==-1 );
  return r;
}

// split line into tokens
vector<string> split(string s){
  stringstream ss(s);
  string token;
  vector<string> v;
  while(ss>>token){
    v.push_back(token);
  }
  return v;
}


class Client_session
  : public std::enable_shared_from_this<Client_session>
{
public:
  Client_session(tcp::socket socket)
    : socket_(std::move(socket))
  {
    count=0;
    socks4a_after_null = false;
  }

  void start()
  {
    do_read_request();
  }

private:
  void do_read_request()
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(onebyte),
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
              cout<<"<S_IP>: "<< socket_.remote_endpoint().address().to_string()<<endl;
              cout<<"<S_PORT>: "<<socket_.remote_endpoint().port()<<endl;
              if(socks4a_after_null)
              {
                hostent *he = gethostbyname (strbuf.substr(FirstNullPlace).c_str());
                if(!he)
                {
                  cerr<<"cannot resolve domain name : "+strbuf.substr(FirstNullPlace)<<endl;
                }
                cout<<"<D_IP>: "<<inet_ntoa (*((struct in_addr *) he->h_addr_list[0]))<<endl;
              }else{
                cout<<"<D_IP>: "<<int(strbuf[4])<<":"<<int(strbuf[5])<<":"<<int(strbuf[6])<<":"<<int(strbuf[7])<<endl;
              }
              cout<<"<D_PORT>: "<<strbuf[2]*256+strbuf[3]<<endl;
              if(strbuf[1]=='\x01')cout<<"<Command>: "<<"CONNECT\n";
              if(strbuf[1]=='\x02')cout<<"<Command>: "<<"BIND\n";
              cout<<"<Reply>: "<<"Accept"<<endl;
              do_accept_request(int(strbuf[1]));
            }
        });
  }

  void do_accept_request(int cd)
  {
    if(cd == 1)
    {
      
    }
  }

  void do_write(std::size_t bytes_transferred)
  {
  }

  int FirstNullPlace;
  bool socks4a_after_null;
  int count;
  string strbuf;
  std::array<char, 1> onebyte;
  tcp::socket socket_;
};

class Server_session
  : public std::enable_shared_from_this<Server_session>
{
public:
  Server_session(tcp::socket socket)
    : socket_(std::move(socket))
  {
    count=0;
    socks4a_after_null = false;
  }

  void start()
  {
    do_read_request();
  }

private:
  void do_read_request()
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(onebyte),
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
              cout<<"<S_IP>: "<< socket_.remote_endpoint().address().to_string()<<endl;
              cout<<"<S_PORT>: "<<socket_.remote_endpoint().port()<<endl;
              if(socks4a_after_null)
              {
                hostent *he = gethostbyname (strbuf.substr(FirstNullPlace).c_str());
                if(!he)
                {
                  cerr<<"cannot resolve domain name : "+strbuf.substr(FirstNullPlace)<<endl;
                }
                cout<<"<D_IP>: "<<inet_ntoa (*((struct in_addr *) he->h_addr_list[0]))<<endl;
              }else{
                cout<<"<D_IP>: "<<int(strbuf[4])<<":"<<int(strbuf[5])<<":"<<int(strbuf[6])<<":"<<int(strbuf[7])<<endl;
              }
              cout<<"<D_PORT>: "<<strbuf[2]*256+strbuf[3]<<endl;
              if(strbuf[1]=='\x01')cout<<"<Command>: "<<"CONNECT\n";
              if(strbuf[1]=='\x02')cout<<"<Command>: "<<"BIND\n";
              cout<<"<Reply>: "<<"Accept"<<endl;
              do_accept_request(int(strbuf[1]));
            }
        });
  }

  void do_accept_request(int cd)
  {
    if(cd == 1)
    {
      
    }
  }

  void do_write(std::size_t bytes_transferred)
  {
  }

  int FirstNullPlace;
  bool socks4a_after_null;
  int count;
  string strbuf;
  std::array<char, 1> onebyte;
  tcp::socket socket_;
};



shared_ptr<Client_session> client_side_sess;
shared_ptr<Server_session> server_side_sess;

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
            int pid = fork_until_success();
            if (!ec && pid==0)
            {
                client_side_sess = std::make_shared<Client_session>(std::move(socket));
                client_side_sess->start();
                acceptor_.close(ec);
                return;
            }
            socket.close();
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

    boost::asio::io_context io_context;

    server s(io_context, std::atoi(argv[1]));

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}