#include <iostream>

using namespace std;

int main(){
    string reply = "\x00\x5a"s ;
    reply = reply+char(80)s+char(80)s;
    reply += char(81)+char(81);
    cout<<reply.substr(1).c_str();
    return 0;
}