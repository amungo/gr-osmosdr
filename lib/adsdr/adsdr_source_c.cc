#include "adsdr_source_c.h"

using namespace ADSDR;
using namespace std;

adsdr_source_c_sptr make_adsdr_source_c (const string &args)
{
    return gnuradio::get_initial_sptr(new adsdr_source_c (args));
}

/*
 * Specify constraints on number of input and output streams.
 * This info is used to construct the input and output signatures
 * (2nd & 3rd args to gr_block's constructor).  The input and
 * output signatures are used by the runtime system to
 * check that a valid number and type of inputs and outputs
 * are connected to this block.  In this case, we accept
 * only 0 input and 1 output.
 */
static const int MIN_IN = 0;	// mininum number of input streams
static const int MAX_IN = 0;	// maximum number of input streams
static const int MIN_OUT = 1;	// minimum number of output streams
static const int MAX_OUT = 1;	// maximum number of output streams

adsdr_source_c::adsdr_source_c (const string & args) : gr::sync_block ("adsdr_source_c",
                                                                gr::io_signature::make (MIN_IN, MAX_IN, sizeof (gr_complex)),
                                                                gr::io_signature::make (MIN_OUT, MAX_OUT, sizeof (gr_complex))),
                                                                adsdr_common(args)
{
    if(_sdr == nullptr)
    {
        throw runtime_error("ADSDR not initialized!");
    }
}

bool adsdr_source_c::start()
{
    cout << " *************** adsdr_source::start() ***************" << endl;
    response res = _sdr->send_cmd({SET_DATAPATH_EN, 1});
    if(res.error != CMD_OK)
    {
        cerr << " Could not set data path enabled. Error!! " << endl;
        return false;
    }
    _sdr->start_rx(std::bind(&adsdr_source_c::adsdr_rx_callback, this, std::placeholders::_1));

    _running = true;

    return true;
}

bool adsdr_source_c::stop()
{
    _sdr->send_cmd({SET_DATAPATH_EN, 0});
    _sdr->stop_rx();

    _running = false;

    return true;
}

void adsdr_source_c::adsdr_rx_callback(const vector<sample> &samples)
{
    unique_lock<std::mutex> lk(_buf_mut);

    for(const sample &s : samples)
    {
        if(!_buf_queue.try_enqueue(s))
        {
	    if(!_ignore_overflow)
	    {
		throw runtime_error("RX buffer overflow");
	    }
        }
        else
        {
            _buf_num_samples++;
        }
    }

    _buf_cond.notify_one();
}

int adsdr_source_c::work(int noutput_items, gr_vector_const_void_star& input_items, gr_vector_void_star& output_items)
{
    gr_complex *out = static_cast<gr_complex *>(output_items[0]);

    unique_lock<std::mutex> lk(_buf_mut);

    if(!_running)
    {
	return WORK_DONE;
    }

    // Wait until enough samples collected
    while(_buf_num_samples < (unsigned int) noutput_items)
    {
        _buf_cond.wait(lk);
    }

    for(int i = 0; i < noutput_items; ++i)
    {
        sample s;
        if(!_buf_queue.try_dequeue(s))
        {
            // This should not be happening
            throw runtime_error("Failed to get sample from buffer. This should never happen. Number of available samples reported to be " + to_string(_buf_num_samples) + ", noutput_items=" + to_string(noutput_items) + ", i=" + to_string(i));
        }
        else
        {
            _buf_num_samples--;
        }

        out[i] = gr_complex(((float) s.i) / 2048.0f, ((float) s.q) / 2048.0f);
    }

    return noutput_items;
}

double adsdr_source_c::set_sample_rate( double rate )
{
    command cmd = _sdr->make_command(SET_RX_SAMP_FREQ, rate);
    response r = _sdr->send_cmd(cmd);
    if(r.error != CMD_OK)
    {
        cerr << "Could not set RX sample rate, error: " << r.error << endl;
        return 0;
    }
    else
    {
        return static_cast<double>(r.param);
    }
}

double adsdr_source_c::get_sample_rate( void )
{
    response r = _sdr->send_cmd({GET_RX_SAMP_FREQ, 0});
    if(r.error != CMD_OK)
    {
        cerr << "Could not get RX sample rate, error: " << r.error << endl;
        return 0;
    }
    else
    {
        return static_cast<double>(r.param);
    }
}

double adsdr_source_c::set_center_freq( double freq, size_t chan )
{
    command cmd = _sdr->make_command(SET_RX_LO_FREQ, freq);
    response r = _sdr->send_cmd(cmd);
    if(r.error != CMD_OK)
    {
        cerr << "Could not set RX LO frequency, error: " << r.error << endl;
        return 0;
    }
    else
    {
        return static_cast<double>(r.param);
    }
}

double adsdr_source_c::get_center_freq( size_t chan )
{
    response r = _sdr->send_cmd({GET_RX_LO_FREQ, 0});
    if(r.error != CMD_OK)
    {
        cerr << "Could not get RX LO frequency, error: " << r.error << endl;
        return 0;
    }
    else
    {
        return static_cast<double>(r.param);
    }
}

vector<string> adsdr_source_c::get_gain_names( size_t chan )
{
    vector<string> names;

    names.push_back("RF");

    return names;
}

osmosdr::gain_range_t adsdr_source_c::get_gain_range(size_t chan)
{
    osmosdr::meta_range_t gain_ranges;

    gain_ranges.push_back(osmosdr::range_t(0, 74, 1));

    return gain_ranges;
}

bool adsdr_source_c::set_gain_mode( bool automatic, size_t chan )
{
    uint8_t gc_mode = RF_GAIN_SLOWATTACK_AGC;

    if(!automatic)
    {
        gc_mode = RF_GAIN_MGC;
    }

    command cmd = _sdr->make_command(SET_RX_GC_MODE, gc_mode);
    response r = _sdr->send_cmd(cmd);
    if(r.error != CMD_OK)
    {
        cerr << "Could not set RX RF gain control mode, error: " << r.error << endl;
        return false;
    }
    else
    {
        return r.param != RF_GAIN_MGC;
    }
}

bool adsdr_source_c::get_gain_mode( size_t chan )
{
    response r = _sdr->send_cmd({GET_RX_GC_MODE, 0});
    if(r.error != CMD_OK)
    {
        cerr << "Could not get RX RF gain control mode, error: " << r.error << endl;
        return false;
    }
    else
    {
        return r.param != RF_GAIN_MGC;
    }
}

osmosdr::gain_range_t adsdr_source_c::get_gain_range(const string& name, size_t chan)
{
    return get_gain_range(chan);
}

double adsdr_source_c::set_gain(double gain, size_t chan)
{
    gain = get_gain_range().clip(gain);

    command cmd = _sdr->make_command(SET_RX_RF_GAIN, gain);
    response r = _sdr->send_cmd(cmd);
    if(r.error != CMD_OK)
    {
        cerr << "Could not set RX RF gain, error: " << r.error << endl;
        return 0;
    }
    else
    {
        return r.param;
    }
}

double adsdr_source_c::set_gain(double gain, const string& name, size_t chan)
{
    if(name == "RF")
    {
        return set_gain(gain, chan);
    }
    else
    {
        return 0;
    }
}

double adsdr_source_c::get_gain(size_t chan)
{
    response r = _sdr->send_cmd({GET_RX_RF_GAIN, 0});
    if(r.error != CMD_OK)
    {
        cerr << "Could not get RX RF gain, error: " << r.error << endl;
        return 0;
    }
    else
    {
        return (static_cast<double>(r.param));
    }
}

double adsdr_source_c::get_gain(const string& name, size_t chan)
{
    if(name == "RF")
    {
        return get_gain(chan);
    }
    else
    {
        return 0;
    }
}

double adsdr_source_c::set_bb_gain(double gain, size_t chan)
{
    return set_gain(gain, chan);
}

vector<string> adsdr_source_c::get_antennas(size_t chan)
{
    vector<string> antennas;

    antennas.push_back(get_antenna(chan));

    return antennas;
}

string adsdr_source_c::set_antenna(const string& antenna, size_t chan)
{
    return get_antenna(chan);
}

string adsdr_source_c::get_antenna(size_t chan)
{
    return "RX";
}

double adsdr_source_c::set_bandwidth(double bandwidth, size_t chan)
{
    command cmd = _sdr->make_command(SET_RX_RF_BANDWIDTH, bandwidth);
    response r = _sdr->send_cmd(cmd);
    if(r.error != CMD_OK)
    {
        cerr << "Could not set RX RF bandwidth, error: " << r.error << endl;
        return 0;
    }
    else
    {
        return static_cast<double>(r.param);
    }
}

double adsdr_source_c::get_bandwidth(size_t chan)
{
    response r = _sdr->send_cmd({GET_RX_RF_BANDWIDTH, 0});
    if(r.error != CMD_OK)
    {
        cerr << "Could not get RX RF bandwidth, error: " << r.error << endl;
        return 0;
    }
    else
    {
        return static_cast<double>(r.param);
    }
}
