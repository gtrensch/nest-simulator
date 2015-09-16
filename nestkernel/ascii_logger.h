#ifndef ASCII_LOGGER_H
#define ASCII_LOGGER_H

#include <map>
#include <fstream>

#include "logger.h"

namespace nest
{

class ASCIILogger : public Logger
{
public:
  ASCIILogger()
    : files_()
  {
  }

  ~ASCIILogger() throw()
  {
    // FIXME: close remaining files
  }

  void enroll( RecordingDevice& device );
  void enroll( RecordingDevice& device, const std::vector< Name >& value_names );

  void initialize();
  void finalize();
  void synchronize() {}

  void write( const RecordingDevice& device, const Event& event );
  void write( const RecordingDevice& device, const Event& event, const std::vector< double_t >& );

  void set_status( const DictionaryDatum& );
  void get_status( DictionaryDatum& ) const;

private:
  const std::string build_filename_( const RecordingDevice& device ) const;

  struct Parameters_
  {
    long precision_;

    std::string file_ext_;      //!< the file name extension to use, without .
    long fbuffer_size_;         //!< the buffer size to use when writing to file
    long fbuffer_size_old_;     //!< the buffer size to use when writing to file (old)
    bool close_after_simulate_; //!< if true, finalize() shall close the stream
    bool flush_after_simulate_; //!< if true, finalize() shall flush the stream

    Parameters_();

    void get( const ASCIILogger&, DictionaryDatum& ) const;
    void set( const ASCIILogger&, const DictionaryDatum& );
  };

  Parameters_ P_;

  // one map for each virtual process,
  // in turn containing one ostream for everydevice
  // vp -> (gid -> [device, filestream])
  typedef std::map< int, std::map< int, std::pair< RecordingDevice*, std::ofstream* > > > file_map;
  file_map files_;
};

inline void
ASCIILogger::get_status( DictionaryDatum& d ) const
{
  P_.get( *this, d );
}

} // namespace

#endif // ASCII_LOGGER_H