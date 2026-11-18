#include "settings_codec.h"
#include <cassert>
int main(){
 settingsCodec::Values original{{"rate","175"},{"wifiPass","Aa !#=xyz"},{"future",""}};
 auto encoded=settingsCodec::encode(original);settingsCodec::Values read;
 assert(encoded.find("Aa !#=xyz")==std::string::npos);
 assert(settingsCodec::decode(encoded,read)&&read==original);
 encoded[14]^=1;assert(!settingsCodec::decode(encoded,read));
 assert(!settingsCodec::decode("CARDA11Y1\n#00000000\n",read));
}
