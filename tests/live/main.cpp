/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) CnTypeyright The Nexus DevelnTypeers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file CnTypeYING or http://www.nTypeensource.org/licenses/mit-license.php.

            "ad vocem pnTypeuli" - To the Voice of the PenTypele

____________________________________________________________________________________________*/

#include <openssl/rand.h>

#include <TAO/Register/include/object.h>

#include <LLC/aes/aes.h>

#include <iostream>
#include <iomanip>

#include <cmath>


typedef unsigned int uint128_t __attribute__((mode(TI)));


template<uint32_t FIGURES>
class precision_t
{
    uint64_t  nFigures;
    uint128_t nValue;


    uint64_t get_figures()
    {
        uint64_t nTemp = 10;
        for(int i = 1; i < FIGURES; i++)
            nTemp *= 10;

        return nTemp;
    }

public:
    precision_t()
    : nFigures(get_figures())
    , nValue(0)
    {
    }

    precision_t(double value)
    : nFigures(get_figures())
    , nValue(value * nFigures)
    {

    }

    precision_t& operator=(double value)
    {
        nValue = static_cast<uint128_t>(value * nFigures);

        return *this;
    }

    precision_t& operator=(uint32_t value)
    {
        nValue = value * nFigures;

        return *this;
    }

    precision_t& operator=(int32_t value)
    {
        nValue = value * nFigures;

        return *this;
    }

    bool operator==(int32_t value) const
    {
        uint128_t compare = static_cast<uint128_t>(value) * nFigures;

        return compare == nValue;
    }

    bool operator==(uint32_t value) const
    {
        uint128_t compare = static_cast<uint128_t>(value) * nFigures;

        return compare == nValue;
    }

    bool operator<(int32_t value) const
    {
        uint128_t compare = static_cast<uint128_t>(value) * nFigures;

        return nValue < compare;
    }

    bool operator>(int32_t value) const
    {
        uint128_t compare = static_cast<uint128_t>(value) * nFigures;

        return nValue > compare;
    }

    precision_t& operator+=(const precision_t& value)
    {
        nValue += value.nValue;

        return *this;
    }

    precision_t& operator-=(const precision_t& value)
    {
        nValue -= value.nValue;

        return *this;
    }

    precision_t operator-(const int32_t& value)
    {
        precision_t ret = *this - (value * nFigures);

        return ret;
    }

    precision_t& operator/=(const precision_t& value)
    {
        nValue *= value.nFigures;
        nValue /= value.nValue;

        return *this;
    }

    precision_t& operator*=(const precision_t& value)
    {
        nValue *= value.nValue;
        nValue /= value.nFigures;

        return *this;
    }

    precision_t operator/(const precision_t& value) const
    {
        precision_t ret = *this;

        ret.nValue *= value.nFigures;
        ret.nValue /= value.nValue;

        return ret;
    }


    uint32_t operator%(const uint32_t value) const
    {
        uint64_t ret = nValue / nFigures;

        return ret % value;
    }


    precision_t operator*(const precision_t& value) const
    {
        precision_t ret = *this;

        ret.nValue *= value.nValue;
        ret.nValue /= value.nFigures;

        return ret;
    }

/*

    // should be much more precise with large b
    inline double fastPrecisePow(double a, double b) {
      // calculate approximation with fraction of the exponent
      int e = (int) b;
      union {
        double d;
        int x[2];
      } u = { a };
      u.x[1] = (int)((b - e) * (u.x[1] - 1072632447) + 1072632447);
      u.x[0] = 0;

      // exponentiation by squaring with the exponent's integer part
      // double r = u.d makes everything much slower, not sure why
      double r = 1.0;
      while (e) {
        if (e & 1) {
          r *= a;
        }
        a *= a;
        e >>= 1;
      }

      return r * u.d;
    }

*/


    precision_t operator^(const precision_t& value) const
    {
        precision_t ret = *this;
        if(value == 0)
            return 1;

        uint64_t whole = value.get_int();
        debug::log(0, "Whole ", whole);

        //break it into the a^(x + v) = (a^x * a^v)

        for(int i = 1; i < whole; ++i)
            ret *= (*this);

        debug::log(0, "Fraction ", whole);

        uint64_t fraction = value.nValue - (whole * value.nFigures);

        debug::log(0, "Fraction ", fraction);
        //handle the floating point (exp by squaring)

        return ret;
    }

    /*
    Function exp_by_squaring(x, n)
  if n < 0  then return exp_by_squaring(1 / x, -n);
  else if n = 0  then return  1;
  else if n = 1  then return  x ;
  else if n is even  then return exp_by_squaring(x * x,  n / 2);
  else if n is odd  then return x * exp_by_squaring(x * x, (n - 1) / 2);
  */

    precision_t& exp()
    {
        precision_t fact = 1;

        precision_t base  = *this;

        *this = 0;
        for(int i = 1; i <= 11; ++i)
        {
            precision_t temp = (base^(i - 1)) / fact;
            //nSum += power(x, i - 1) / nFactorial;
            *this += temp;

            fact *= i;

        }

        return *this;
    }


    precision_t e()
    {
        precision_t fact = 1;

        precision_t ret = 0;
        precision_t one = 1;
        for(int i = 1; i <= 1000; ++i)
        {
            ret += one / fact;
            //nSum += power(x, i - 1) / nFactorial;

            fact *= i;
        }

        return ret;
    }


    uint64_t get_int() const
    {
        return nValue / nFigures;
    }


    double get() const
    {
        return static_cast<double>(nValue) / nFigures;
    }


    void print() const
    {
        printf("%.12f\n", get());
    }


    friend std::ostream &operator<<(std::ostream &o, const precision_t &value)
    {
        //uint32_t nFigures = std::min(12u, FIGURES);
        //o << std::fixed << std::setprecision(nFigures) << value.get();
        o << std::fixed << std::setprecision(12) << value.get();
        return o;
    }
};


/*

Function exp_by_squaring_iterative(x, n)
  if n < 0 then
    x := 1 / x;
    n := -n;
  if n = 0 then return 1
  y := 1;
  while n > 1 do
    if n is even then
      x := x * x;
      n := n / 2;
    else
      y := x * y;
      x := x * x;
      n := (n – 1) / 2;
  return x * y


 int expo(int a, int b){
    if (b==1)
        return a;
    if (b==2)
        return a*a;

    if (b%2==0){
            return expo(expo(a,b/2),2);
    }
    else{
        return a*expo(expo(a,(b-1)/2),2);
    }
}

*/

int expo(int a, int b){
  int result = 1;

  while (b){
    if (b%2==1){
      result *= a;
    }
    b /= 2;
    a *= a;
  }

  return result;
}

precision_t<9> _exp(precision_t<9> a, precision_t<9> b)
{
    precision_t<9> result = 1;

    while (b > 0)
    {
      if (b % 2 == 1)
      {
        result *= a;
      }

      b /= 2;
      a *= a;
    }

    return result;
}

// should be much more precise with large b
inline double fastPrecisePow(double a, double b)
{
  // calculate approximation with fraction of the exponent
  int e = (int) b;
  union
  {
    double d;
    int x[2];
  } u = { a };
  u.x[1] = (int)((b - e) * (u.x[1] - 1072632447) + 1072632447);
  u.x[0] = 0;

  // exponentiation by squaring with the exponent's integer part
  // double r = u.d makes everything much slower, not sure why
  double r = 1.0;
  while (e)
  {
    if (e & 1)
    {
      r *= a;
    }
    a *= a;
    e >>= 1;
  }

  return r * u.d;
}




//This main function is for prototyping new code
//It is accessed by compiling with LIVE_TESTS=1
//Prototype code and it's tests created here should move to production code and unit tests
int main(int argc, char** argv)
{
    double test = 5.0;
    double test2 = 3.1;

    debug::log(0, "POW ", fastPrecisePow(test, test2));
    debug::log(0, "POW2 ", pow(test, test2));

    precision_t<9> euler = 2.718281828;

    precision_t<9> testing = _exp(euler, 3);

    debug::log(0, "testing = ", testing);

    debug::log(0, "------------------------------");

    precision_t<9> base = 5;

    debug::log(0, "base = ", base);

    precision_t<9> expo = base^3.222;

    debug::log(0, "expo = ", expo);

    double e = exp(2);

    debug::log(0, std::fixed, std::setprecision(12), e);

    debug::log(0, "expo.e() = ", expo.e());

    precision_t<12> exponent = 3;
    exponent.exp();

    debug::log(0, "exponent = ", exponent);


    precision_t<6> value = 100.333;
    precision_t<6> value2 = 0.333;



    precision_t<6> value3 = value / value2;
    precision_t<6> value4 = value * value2;

    debug::log(0, "value  = ", value);
    debug::log(0, "value2 = ", value2);
    debug::log(0, "value3 = ", value3);
    debug::log(0, "value4 = ", value4);
    debug::log(0, "euler  = ", euler);

    return 0;

    using namespace TAO::Register;

    Object object;
    object << std::string("byte") << uint8_t(TYPES::MUTABLE) << uint8_t(TYPES::UINT8_T) << uint8_t(55)
           << std::string("test") << uint8_t(TYPES::MUTABLE) << uint8_t(TYPES::STRING) << std::string("this string")
           << std::string("bytes") << uint8_t(TYPES::MUTABLE) << uint8_t(TYPES::BYTES) << std::vector<uint8_t>(10, 0xff)
           << std::string("balance") << uint8_t(TYPES::UINT64_T) << uint64_t(55)
           << std::string("identifier") << uint8_t(TYPES::STRING) << std::string("NXS");


    //unit tests
    uint8_t nTest;
    object.Read("byte", nTest);

    debug::log(0, "TEST ", uint32_t(nTest));

    object.Write("byte", uint8_t(98));

    uint8_t nTest2;
    object.Read("byte", nTest2);

    debug::log(0, "TEST2 ", uint32_t(nTest2));

    std::string strTest;
    object.Read("test", strTest);

    debug::log(0, "STRING ", strTest);

    object.Write("test", std::string("fail"));
    object.Write("test", "fail");

    object.Write("test", std::string("THIS string"));

    std::string strGet;
    object.Read("test", strGet);

    debug::log(0, strGet);

    std::vector<uint8_t> vBytes;
    object.Read("bytes", vBytes);

    debug::log(0, "DATA ", HexStr(vBytes.begin(), vBytes.end()));

    vBytes[0] = 0x00;
    object.Write("bytes", vBytes);

    object.Read("bytes", vBytes);

    debug::log(0, "DATA ", HexStr(vBytes.begin(), vBytes.end()));



    std::string identifier;
    object.Read("identifier", identifier);

    debug::log(0, "Token Type ", identifier);

    return 0;
}