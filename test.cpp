#include<iostream>
#include<cmath>
#include "noisemake.h"
#include <windows.h>
using namespace std;


double dp=0;
double dp2=2*M_PI*14;
double dp3=0;

double MakeNoise(double dTime)
{
    double out1=10*sin(dp2*dTime);
    double out2=10*sin(dp*dTime);
    double out3=10*sin(dp3*dTime);
    return out2+out1+out3;
 
}

int main()
{
    string a("Speakers (Realtek(R) Audio)");
    olcNoiseMaker<short> snd(a,44100,1,8,512);

    snd.SetUserFunction(MakeNoise);
    while(1)
    {
        if(GetAsyncKeyState(0x30))
            dp=2*M_PI*50;
        else if(GetAsyncKeyState(0x31))
            dp=2*M_PI*100;
        else if(GetAsyncKeyState(0x32))
            dp=2*M_PI*150;
        else if(GetAsyncKeyState(0x33))
            dp=2*M_PI*175;
        else if(GetAsyncKeyState(0x34))
            dp=2*M_PI*200;
        else if(GetAsyncKeyState(0x35))
            dp=2*M_PI*225;
        else if(GetAsyncKeyState(0x36))
            dp=2*M_PI*250;
        else if(GetAsyncKeyState(0x37))
            dp=2*M_PI*275;
        else if(GetAsyncKeyState(0x38))
            dp=2*M_PI*300;
        else if(GetAsyncKeyState(0x39))
            dp3=3*M_PI*10;
        else
            dp=0;
    }
    return 0;
}