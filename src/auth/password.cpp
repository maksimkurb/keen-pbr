#include "password.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace keen_pbr3::auth {
namespace {

using Bytes = std::vector<std::uint8_t>;
constexpr std::uint32_t kIterations = 200000;

constexpr std::array<std::uint32_t, 64> K{{
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2}};

std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

Bytes sha256(const Bytes& input) {
  Bytes data = input;
  const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
  data.push_back(0x80);
  while ((data.size() % 64) != 56) data.push_back(0);
  for (int i = 7; i >= 0; --i) data.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
  std::array<std::uint32_t, 8> h{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                  0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
  for (std::size_t off = 0; off < data.size(); off += 64) {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
      const auto p = off + static_cast<std::size_t>(i) * 4;
      w[i] = (std::uint32_t(data[p]) << 24) | (std::uint32_t(data[p+1]) << 16) |
             (std::uint32_t(data[p+2]) << 8) | data[p+3];
    }
    for (int i = 16; i < 64; ++i) {
      const auto s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
      const auto s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; ++i) {
      const auto s1=rotr(e,6)^rotr(e,11)^rotr(e,25), ch=(e&f)^((~e)&g);
      const auto t1=hh+s1+ch+K[i]+w[i], s0=rotr(a,2)^rotr(a,13)^rotr(a,22);
      const auto maj=(a&b)^(a&c)^(b&c), t2=s0+maj;
      hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
  }
  Bytes out(32);
  for (int i=0;i<8;++i) for(int j=0;j<4;++j) out[i*4+j]=static_cast<std::uint8_t>(h[i]>>(24-j*8));
  return out;
}

Bytes hmac(const Bytes& key, const Bytes& msg) {
  Bytes k=key; if(k.size()>64) k=sha256(k); k.resize(64,0);
  Bytes inner(64), outer(64);
  for(int i=0;i<64;++i){inner[i]=k[i]^0x36;outer[i]=k[i]^0x5c;}
  inner.insert(inner.end(),msg.begin(),msg.end()); auto ih=sha256(inner);
  outer.insert(outer.end(),ih.begin(),ih.end()); return sha256(outer);
}

Bytes pbkdf2(std::string_view password, const Bytes& salt, std::uint32_t iterations) {
  Bytes key(password.begin(),password.end()), msg=salt;
  msg.insert(msg.end(),{0,0,0,1}); Bytes u=hmac(key,msg), out=u;
  for(std::uint32_t i=1;i<iterations;++i){u=hmac(key,u);for(std::size_t j=0;j<out.size();++j)out[j]^=u[j];}
  return out;
}

const char* B64="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
std::string b64(const Bytes& in) {
  std::string out; std::uint32_t v=0; int bits=-6;
  for(auto c:in){v=(v<<8)|c;bits+=8;while(bits>=0){out.push_back(B64[(v>>bits)&63]);bits-=6;}}
  if(bits>-6)out.push_back(B64[((v<<8)>>(bits+8))&63]); return out;
}
Bytes unb64(std::string_view in) {
  std::array<int,256> map{};map.fill(-1);for(int i=0;i<64;++i)map[static_cast<unsigned char>(B64[i])]=i;
  Bytes out;std::uint32_t v=0;int bits=-8;
  for(unsigned char c:in){if(map[c]<0)throw std::invalid_argument("base64");v=(v<<6)|map[c];bits+=6;if(bits>=0){out.push_back((v>>bits)&255);bits-=8;}}
  return out;
}
Bytes random_bytes(std::size_t n) {
  Bytes out(n);std::ifstream f("/dev/urandom",std::ios::binary);f.read(reinterpret_cast<char*>(out.data()),n);
  if(!f)throw std::runtime_error("failed to read secure random bytes");return out;
}
bool parse(std::string_view value,std::uint32_t& iterations,Bytes& salt,Bytes& digest){
  try { std::string s(value);auto a=s.find('$'),b=s.find('$',a+1),c=s.find('$',b+1);
    if(a==std::string::npos||b==std::string::npos||c==std::string::npos||s.substr(0,a)!="pbkdf2-sha256")return false;
    std::size_t used=0;auto raw=s.substr(a+1,b-a-1);auto count=std::stoul(raw,&used);if(used!=raw.size()||count<100000||count>2000000)return false;
    iterations=static_cast<std::uint32_t>(count);salt=unb64(s.substr(b+1,c-b-1));digest=unb64(s.substr(c+1));return salt.size()==16&&digest.size()==32;
  } catch(...){return false;}
}
}

bool constant_time_equal(std::string_view a,std::string_view b){std::size_t n=a.size()>b.size()?a.size():b.size();unsigned diff=unsigned(a.size()^b.size());for(std::size_t i=0;i<n;++i)diff|=(i<a.size()?a[i]:0)^(i<b.size()?b[i]:0);return diff==0;}
std::string generate_password_hash(std::string_view password){auto salt=random_bytes(16);return "pbkdf2-sha256$"+std::to_string(kIterations)+"$"+b64(salt)+"$"+b64(pbkdf2(password,salt,kIterations));}
bool valid_password_hash(std::string_view encoded){std::uint32_t i;Bytes s,d;return parse(encoded,i,s,d);}
bool verify_password(std::string_view password,std::string_view encoded){std::uint32_t i;Bytes s,d;if(!parse(encoded,i,s,d))return false;return constant_time_equal(b64(pbkdf2(password,s,i)),b64(d));}
std::string random_token(){return b64(random_bytes(32));}
std::string sha256_hex(std::string_view value){Bytes in(value.begin(),value.end());auto d=sha256(in);static const char* hex="0123456789abcdef";std::string out;out.reserve(64);for(auto b:d){out.push_back(hex[b>>4]);out.push_back(hex[b&15]);}return out;}

} // namespace keen_pbr3::auth
