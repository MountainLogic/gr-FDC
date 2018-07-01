/* -*- c++ -*- */
/* 
 * Copyright 2018 Gereon Such.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include "SegmentDetection_impl.h"

namespace gr {
namespace FDC {

SegmentDetection::sptr
SegmentDetection::make(int ID, int blocklen, int relinvovl, float seg_start, float seg_stop, float thresh, float minchandist, float window_flank_puffer, int maxblocks_to_emit, int channel_deactivation_delay, bool messageoutput, bool fileoutput, std::string path, bool threads, int verbose)
{
    return gnuradio::get_initial_sptr
            (new SegmentDetection_impl(ID, blocklen, relinvovl, seg_start, seg_stop, thresh, minchandist, window_flank_puffer, maxblocks_to_emit, channel_deactivation_delay, messageoutput, fileoutput, path, threads, verbose));
}

/*
     * The private constructor
     */
SegmentDetection_impl::SegmentDetection_impl(int ID, int blocklen, int relinvovl, float seg_start, float seg_stop, float thresh, float minchandist, float window_flank_puffer, int maxblocks_to_emit, int channel_deactivation_delay, bool messageoutput, bool fileoutput, std::string path, bool threads, int verbose)
    : gr::sync_block("SegmentDetection",
                     gr::io_signature::make(1, 1, sizeof(gr_complex)*blocklen),
                     gr::io_signature::make(0, 0, 0))
{
    d_ID=ID;

    //set logging and runtime mode
    d_threading=threads;
    if((VERBOSE) verbose==LOGTOFILE){
        d_verbose=LOGTOFILE;
        d_logfile=std::string("gr-FDC.ActDetChan.ID_") + std::to_string(d_ID) + std::string(".log");
        FILE *f=fopen(d_logfile.c_str(), "w");
        if(!f)
            std::cerr << "Logfile not writable: " << d_logfile << std::endl;
        else
            fwrite("\n", sizeof(char), 1, f);
        fclose(f);
    }else if((VERBOSE) verbose==LOGTOCONSOLE)
        d_verbose=LOGTOCONSOLE;
    else
        d_verbose=NOLOG;

    //init base and detection parameters

    d_blocklen=blocklen;
    if(!ispow2(d_blocklen))
        throw std::invalid_argument("Blocklen must be Power of 2. ");

    d_relinvovl=relinvovl;
    if(!ispow2(d_relinvovl))
        throw std::invalid_argument("Relinvovl must be Power of 2. ");

    if(thresh<0.0f)
        throw std::invalid_argument("Threshold is interpreted as dB and must be greater zero to detect channels accordingly. ");
    d_thresh=(float) pow( 10.0, (double)thresh/10.0 );

    d_blockcount=0;
    d_hist.resize(d_blocklen, gr_complex(0.0f, 0.0f));

    d_maxblocks=maxblocks_to_emit;
    d_channel_deactivation_delay=channel_deactivation_delay;

    d_window_flank_puffer=window_flank_puffer;
    if(d_window_flank_puffer<0.0)
        throw std::invalid_argument("Window flank puffer must not be smaller 0.0. \n");

    //set detection start, stop, width and detection decimation factor
    set_chan_start_stop_width_dec(seg_start, seg_stop, minchandist);

    //create all possible windows for blocklen
    cr_windows();



    //set output parameters
    d_msg_output=messageoutput;
    if(d_msg_output){
        d_msgport=pmt::intern("msgout");
        message_port_register_out(d_msgport);
    }

    d_file_output=fileoutput;
    if(d_file_output){
        //possible plausibility/writability check here?
        d_fileoutput_path=path;
    }

    log(std::string("Threshold               ") + std::to_string(d_thresh));
    log(std::string("decimation factor       ") + std::to_string(d_chan_detection_decimation_factor));
    log(std::string("start                   ") + std::to_string(d_start));
    log(std::string("stop                    ") + std::to_string(d_stop));
    log(std::string("width                   ") + std::to_string(d_width));





}

/*
     * Our virtual destructor.
     */
SegmentDetection_impl::~SegmentDetection_impl()
{
}

int
SegmentDetection_impl::work(int noutput_items,
                            gr_vector_const_void_star &input_items,
                            gr_vector_void_star &output_items)
{
    const gr_complex *in = (const gr_complex *) input_items[0];

    const gr_complex *sig;
    const gr_complex *sig_hist = d_hist.data(); //pointer to last block, initialized with saved history.

    for(int i=0;i<noutput_items;i++){ //1 item i = 1 block
        sig=in+i*d_blocklen; //sig is current block

        //channel detection HERE

        //finally iterate history pointer
        sig_hist=sig;

        d_blockcount++;
    }

    //saving last block to history puffer
    save_hist(sig);

    // Tell runtime system how many output items we produced.
    return noutput_items;
}


void SegmentDetection_impl::cr_windows(){
    int num_window_sizes=(int) log2((double) d_blocklen)+1;
    int winwidth;
    int puffersamples;
    float flank;

    d_windows.resize(num_window_sizes);

    //d_windows is vec[vec[vec[complex]]]
    //meaning: window_length, phase_shift, samples[complex]

    for(int winsize=0;winsize<num_window_sizes;winsize++){
        d_windows[winsize].resize(d_relinvovl);

        winwidth=1<<winsize;

        puffersamples=(int)(d_window_flank_puffer*(double)winwidth);

        for(int i=0;i<d_relinvovl;i++){
            //create all phases for each window width
            d_windows[winsize][i].resize(winwidth, gr_complex( std::polar(1.0, 2.0 * M_PI * (double)i / (double)d_relinvovl ) ) );

            //create flank
            for(int k=0;k<puffersamples;k++){
                flank=0.5f - 0.5f*(float)cos(M_PI*(double)k/(double)puffersamples);
                d_windows[winsize][i][k]*=flank;
                d_windows[winsize][i][winwidth -1 -k]*=flank;
            }

        }

    }
}

void SegmentDetection_impl::save_hist(const gr_complex *in){
    if(d_hist.size()!=d_blocklen)
        d_hist.resize(d_blocklen, gr_complex(0.0f, 0.0f) );

    memcpy(d_hist.data(), in, d_blocklen*sizeof(gr_complex) );
}

void SegmentDetection_impl::set_chan_start_stop_width_dec(float start, float stop, float minchandist){
    //plausibility checks
    minchandist=mod_f(minchandist, 1.0f);
    start=mod_f(start, 1.0f);
    stop=mod_f(stop, 1.0f);

    if(start==stop)
        throw std::invalid_argument("Start must not be equal to stop. ");

    if(start>stop){
        // swapping start and stop.
        float tmp=start;
        start=stop;
        stop=tmp;
    }

    //set decimation factor by minimum channel distance
    double dec=(double)d_blocklen * (double)minchandist /2.0;

    if(dec<2.0)
        d_chan_detection_decimation_factor=1;
    else
        //chan_detection_decimation_factor=lastpow2(dec);
        d_chan_detection_decimation_factor=(int) dec;

    //determine width by decimation factor
    size_t width=(size_t) ((double)(stop-start) * (double) d_blocklen);
    if(width%d_chan_detection_decimation_factor)
        width+=d_chan_detection_decimation_factor - width%d_chan_detection_decimation_factor;
    if(width>d_blocklen) //this should not happen, but to be sure.
        width=d_blocklen - (d_blocklen%d_chan_detection_decimation_factor);

    d_width=width;

    //determine extraction start and stop
    size_t mid=(size_t) ((double) (0.5f*(start+stop)) * (double) d_blocklen);
    d_start=mid<d_width/2 ? 0 : mid-d_width/2; //start minimum at 0
    d_stop=d_start+d_width;
    if(d_stop > d_blocklen){
        d_stop=d_blocklen;
        d_start=d_stop-d_blocklen;
    }

    //reset power size.
    d_power.resize(d_width / d_chan_detection_decimation_factor);
}

void SegmentDetection_impl::fftshift(gr_complex *in, gr_complex *out, int sz){
    if(sz%2)
        sz--;

    int N2=sz/2;

    memcpy( out, in+N2, N2*sizeof(gr_complex) );
    memcpy( out+N2, in, N2*sizeof(gr_complex) );
}

void SegmentDetection_impl::log(std::string s){
    if(d_verbose==LOGTOCONSOLE)
        std::cout << s << std::endl;
    else if(d_verbose==LOGTOFILE){
        FILE *f=fopen(d_logfile.c_str(), "a");
        if(!f)
            std::cerr << "Outputfile not writable: " << d_logfile << std::endl;
        else{
            s+=std::string("\n");
            fwrite(s.c_str(), sizeof(char), s.size(), f);
        }
        fclose(f);
    }
}


/*
 * Additional helping functions
 */

template <typename T>
float mod_f(T x, T y){
    return (float) fmod(fmod((double) x,(double) y)+1.0, (double) y);
}

template <typename T>
int nextpow2(T v){
    return 1<<(int) ceil(log2((double) v));
}

template <typename T>
int lastpow2(T v){
    return 1<<(int) floor(log2((double) v));
}

template <typename T>
bool ispow2(T v){
    if( v == (T) nextpow2(v) )
        return true;
    return false;
}


} /* namespace FDC */
} /* namespace gr */

