//------------------------------------------------------------------------------
// Copyright (c) 2011-2014 by European Organization for Nuclear Research (CERN)
// Author: Lukasz Janyst <ljanyst@cern.ch>
//------------------------------------------------------------------------------
// This file is part of the XRootD software suite.
//
// XRootD is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// XRootD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with XRootD.  If not, see <http://www.gnu.org/licenses/>.
//
// In applying this licence, CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
//------------------------------------------------------------------------------

#include "XrdCl/XrdClFileSystem.hh"
#include "XrdCl/XrdClFileSystemUtils.hh"
#include "XrdCl/XrdClFSExecutor.hh"
#include "XrdCl/XrdClURL.hh"
#include "XrdCl/XrdClLog.hh"
#include "XrdCl/XrdClDefaultEnv.hh"
#include "XrdCl/XrdClConstants.hh"
#include "XrdCl/XrdClUtils.hh"
#include "XrdCl/XrdClCopyProcess.hh"
#include "XrdCl/XrdClFile.hh"
#include "XrdCl/XrdClFileSystemOperations.hh"
#include "XrdCl/XrdClParallelOperation.hh"
#include "XrdOuc/XrdOucPrivateUtils.hh"
#include "XrdSys/XrdSysE2T.hh"

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <cmath>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

using namespace XrdCl;

//------------------------------------------------------------------------------
// Build a path
//------------------------------------------------------------------------------
XRootDStatus BuildPath( std::string &newPath, Env *env,
                        const std::string &path )
{
  if( path.empty() )
    return XRootDStatus( stError, errInvalidArgs );

  int noCwd = 0;
  env->GetInt( "NoCWD", noCwd );

  if( path[0] == '/' || noCwd )
  {
    newPath = path;
    return XRootDStatus();
  }

  std::string cwd = "/";
  env->GetString( "CWD", cwd );
  newPath  = cwd;
  newPath += "/";
  newPath += path;

  //----------------------------------------------------------------------------
  // Collapse the dots
  //----------------------------------------------------------------------------
  std::list<std::string> pathComponents;
  std::list<std::string>::iterator it;
  XrdCl::Utils::splitString( pathComponents, newPath, "/" );
  newPath = "/";
  for( it = pathComponents.begin(); it != pathComponents.end(); )
  {
    if( *it == "." )
    {
      it = pathComponents.erase( it );
      continue;
    }

    if( *it == ".." )
    {
      if( it == pathComponents.begin() )
        return XRootDStatus( stError, errInvalidArgs );
      std::list<std::string>::iterator it1 = it;
      --it1;
      it = pathComponents.erase( it1 );
      it = pathComponents.erase( it );
      continue;
    }
    ++it;
  }

  newPath = "/";
  for( it = pathComponents.begin(); it != pathComponents.end(); ++it )
  {
    newPath += *it;
    newPath += "/";
  }
  if( newPath.length() > 1 )
    newPath.erase( newPath.length()-1, 1 );

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Convert mode string to uint16_t
//------------------------------------------------------------------------------
XRootDStatus ConvertMode( Access::Mode &mode, const std::string &modeStr )
{
  if( modeStr.length() != 9 )
    return XRootDStatus( stError, errInvalidArgs );

  mode = Access::None;
  for( int i = 0; i < 3; ++i )
  {
    if( modeStr[i] == 'r' )
      mode |= Access::UR;
    else if( modeStr[i] == 'w' )
      mode |= Access::UW;
    else if( modeStr[i] == 'x' )
      mode |= Access::UX;
    else if( modeStr[i] != '-' )
      return XRootDStatus( stError, errInvalidArgs );
  }
  for( int i = 3; i < 6; ++i )
  {
    if( modeStr[i] == 'r' )
      mode |= Access::GR;
    else if( modeStr[i] == 'w' )
      mode |= Access::GW;
    else if( modeStr[i] == 'x' )
      mode |= Access::GX;
    else if( modeStr[i] != '-' )
      return XRootDStatus( stError, errInvalidArgs );
  }
  for( int i = 6; i < 9; ++i )
  {
    if( modeStr[i] == 'r' )
      mode |= Access::OR;
    else if( modeStr[i] == 'w' )
      mode |= Access::OW;
    else if( modeStr[i] == 'x' )
      mode |= Access::OX;
    else if( modeStr[i] != '-' )
      return XRootDStatus( stError, errInvalidArgs );
  }
  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Perform a cache operation
//------------------------------------------------------------------------------
XRootDStatus DoCache( FileSystem                      *fs,
                      Env                             *env,
                      const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 3 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs, 0,
                                  "Wrong number of arguments." );
  }

  if( args[1] != "evict" && args[1] != "fevict")
  {
    log->Error( AppMsg, "Invalid cache operation." );
    return XRootDStatus( stError, errInvalidArgs, 0, "Invalid cache operation." );
  }

  std::string fullPath;
  if( !BuildPath( fullPath, env, args[2] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid cache path." );
    return XRootDStatus( stError, errInvalidArgs, 0, "Invalid cache path." );
  }

  //----------------------------------------------------------------------------
  // Create the command 
  //----------------------------------------------------------------------------
  std::string cmd = args[1];
  cmd.append(" ");
  cmd.append(fullPath);

  //----------------------------------------------------------------------------
  // Run the operation
  //----------------------------------------------------------------------------
  Buffer *response = 0;
  XRootDStatus st = fs->SendCache( cmd, response );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable set cache %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  if( response )
  {
    std::cout << response->ToString() << '\n';
  }

  delete response;

  return XRootDStatus();
}
//------------------------------------------------------------------------------
// Change current working directory
//------------------------------------------------------------------------------
XRootDStatus DoCD( FileSystem                      *fs,
                   Env                             *env,
                   const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log *log = DefaultEnv::GetLog();
  if( args.size() != 2 )
  {
    log->Error( AppMsg, "Invalid arguments. Expected a path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // cd excludes NoCWD
  //----------------------------------------------------------------------------
  env->PutInt( "NoCWD", 0 );

  std::string newPath;
  if( !BuildPath( newPath, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Check if the path exist and is not a directory
  //----------------------------------------------------------------------------
  StatInfo *info;
  XRootDStatus st = fs->Stat( newPath, info );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable to stat the path: %s", st.ToStr().c_str() );
    return st;
  }

  if( !info->TestFlags( StatInfo::IsDir ) )
  {
    log->Error( AppMsg, "%s is not a directory.", newPath.c_str() );
    return XRootDStatus( stError, errInvalidArgs );
  }

  env->PutString( "CWD", newPath );
  delete info;
  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Helper function to calculate number of digits in a number
//------------------------------------------------------------------------------
uint32_t nbDigits( uint64_t nb )
{
  if( nb == 0 ) return 1;
  return uint32_t( log10( double(nb) ) + 1);
}


void PrintDirListStatInfo( StatInfo *info, bool hascks = false, uint32_t ownerwidth = 0, uint32_t groupwidth = 0, uint32_t sizewidth = 0 )
{
  if( info->ExtendedFormat() )
  {
    if( info->TestFlags( StatInfo::IsDir ) )
      std::cout << "d";
    else
      std::cout << "-";
    std::cout << info->GetModeAsOctString();

    std::cout << " " << std::setw( ownerwidth ) << info->GetOwner();
    std::cout << " " << std::setw( groupwidth ) << info->GetGroup();
    std::cout << " " << std::setw( sizewidth ) << info->GetSize();
    if( hascks && info->HasChecksum() )
      std::cout << " " << std::setw( sizewidth ) << info->GetChecksum();
    std::cout << " " << info->GetModTimeAsString() << " ";
  }
  else
  {
    if( info->TestFlags( StatInfo::IsDir ) )
      std::cout << "d";
    else
      std::cout << "-";

    if( info->TestFlags( StatInfo::IsReadable ) )
      std::cout << "r";
    else
      std::cout << "-";

    if( info->TestFlags( StatInfo::IsWritable ) )
      std::cout << "w";
    else
      std::cout << "-";

    if( info->TestFlags( StatInfo::XBitSet ) )
      std::cout << "x";
    else
      std::cout << "-";

    std::cout << " " << info->GetModTimeAsString();

    uint64_t size = info->GetSize();
    int width = nbDigits( size ) + 2;
    if( width < 12 ) width = 12;
    std::cout << std::setw( width ) << info->GetSize() << " ";
  }
}

//------------------------------------------------------------------------------
// List a directory
//------------------------------------------------------------------------------
XRootDStatus DoLS( FileSystem                      *fs,
                   Env                             *env,
                   const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log *log = DefaultEnv::GetLog();
  uint32_t    argc     = args.size();
  bool        stats    = false;
  bool        showUrls = false;
  bool        hascks   = false;
  std::string path;
  DirListFlags::Flags flags = DirListFlags::Locate | DirListFlags::Merge;

  if( argc > 6 )
  {
    log->Error( AppMsg, "Too many arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  for( uint32_t i = 1; i < args.size(); ++i )
  {
    if( args[i] == "-l" )
    {
      stats = true;
      flags |= DirListFlags::Stat;
    }
    else if( args[i] == "-u" )
      showUrls = true;
    else if( args[i] == "-R" )
    {
      flags |= DirListFlags::Recursive;
    }
    else if( args[i] == "-D" )
    {
      // show duplicates
      flags &= ~DirListFlags::Merge;
    }
    else if( args[i] == "-Z" )
    {
      // check if file is a ZIP archive if yes list content
      flags |= DirListFlags::Zip;
    }
    else if( args[i] == "-C" )
    {
      // query checksum for each entry in the directory
      hascks = true;
      stats  = true;
      flags |= DirListFlags::Cksm;
    }
    else
      path = args[i];
  }

  if( showUrls )
    // we don't merge the duplicate entries
    // in case we print the full URL
    flags &= ~DirListFlags::Merge;

  std::string newPath = "/";
  if( path.empty() )
    env->GetString( "CWD", newPath );
  else
  {
    if( !BuildPath( newPath, env, path ).IsOK() )
    {
      log->Error( AppMsg, "Invalid arguments. Invalid path." );
      return XRootDStatus( stError, errInvalidArgs );
    }
  }

  //----------------------------------------------------------------------------
  // Stat the entry so we know if it is a file or a directory
  //----------------------------------------------------------------------------
  log->Debug( AppMsg, "Attempting to stat: %s", newPath.c_str() );

  StatInfo *info = 0;
  XRootDStatus st = fs->Stat( newPath, info );
  std::unique_ptr<StatInfo> ptr( info );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable to stat the path: %s", st.ToStr().c_str() );
    return st;
  }

  if( !info->TestFlags( StatInfo::IsDir ) &&
      !( flags & DirListFlags::Zip ) )
  {
    if( stats )
      PrintDirListStatInfo( info );

    if( showUrls )
    {
      std::string url;
      fs->GetProperty( "LastURL", url );
      std::cout << url;
    }
    std::cout << newPath << std::endl;
    return XRootDStatus();
  }


  //----------------------------------------------------------------------------
  // Ask for the list
  //----------------------------------------------------------------------------
  log->Debug( AppMsg, "Attempting to list: %s", newPath.c_str() );

  DirectoryList *list;
  st = fs->DirList( newPath, flags, list );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable to list the path: %s", st.ToStr().c_str() );
    return st;
  }

  if( st.code == suPartial )
  {
    std::cerr << "[!] Some of the requests failed. The result may be ";
    std::cerr << "incomplete." << std::endl;
  }

  uint32_t ownerwidth = 0, groupwidth = 0, sizewidth = 0, ckswidth = 0;
  DirectoryList::Iterator it;
  for( it = list->Begin(); it != list->End() && stats; ++it )
  {
    StatInfo *info = (*it)->GetStatInfo();
    if( ownerwidth < info->GetOwner().size() )
      ownerwidth = info->GetOwner().size();
    if( groupwidth < info->GetGroup().size() )
      groupwidth = info->GetGroup().size();
    if( sizewidth < nbDigits( info->GetSize() ) )
      sizewidth = nbDigits( info->GetSize() );
    if( ckswidth < info->GetChecksum().size() )
      ckswidth = info->GetChecksum().size();
  }

  //----------------------------------------------------------------------------
  // Print the results
  //----------------------------------------------------------------------------
  for( it = list->Begin(); it != list->End(); ++it )
  {
    if( stats )
    {
      StatInfo *info = (*it)->GetStatInfo();
      if( !info )
        std::cout << "---- 0000-00-00 00:00:00            ? ";
      else
        PrintDirListStatInfo( info, hascks, ownerwidth, groupwidth, sizewidth );
    }
    if( showUrls )
      std::cout << "root://" << (*it)->GetHostAddress() << "/";
    std::cout << list->GetParentName() << (*it)->GetName() << std::endl;
  }
  delete list;
  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Create a directory
//------------------------------------------------------------------------------
XRootDStatus DoMkDir( FileSystem                      *fs,
                      Env                             *env,
                      const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc < 2 || argc > 4 )
  {
    log->Error( AppMsg, "Too few arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  MkDirFlags::Flags flags = MkDirFlags::None;
  Access::Mode mode    = Access::None;
  std::string  modeStr = "rwxr-x---";
  std::string  path    = "";

  for( uint32_t i = 1; i < args.size(); ++i )
  {
    if( args[i] == "-p" )
      flags |= MkDirFlags::MakePath;
    else if( !args[i].compare( 0, 2, "-m" ) )
      modeStr = args[i].substr( 2, 9 );
    else
      path = args[i];
  }

  XRootDStatus st = ConvertMode( mode, modeStr );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Invalid mode string." );
    return st;
  }

  std::string newPath;
  if( !BuildPath( newPath, env, path ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  st = fs->MkDir( newPath, flags, mode );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable create directory %s: %s",
                        newPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Remove a directory
//------------------------------------------------------------------------------
XRootDStatus DoRmDir( FileSystem                      *query,
                      Env                             *env,
                      const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 2 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath;
  if( !BuildPath( fullPath, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  XRootDStatus st = query->RmDir( fullPath );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable remove directory %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Move a file or directory
//------------------------------------------------------------------------------
XRootDStatus DoMv( FileSystem                      *fs,
                   Env                             *env,
                   const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 3 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath1;
  if( !BuildPath( fullPath1, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid source path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath2;
  if( !BuildPath( fullPath2, env, args[2] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid destination path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  if( is_subdirectory(fullPath1, fullPath2) )
    return XRootDStatus( stError, errInvalidArgs, 0,
      "cannot move directory to a subdirectory of itself." );

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  XRootDStatus st = fs->Mv( fullPath1, fullPath2 );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable move %s to %s: %s",
                        fullPath1.c_str(), fullPath2.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Remove a file
//------------------------------------------------------------------------------
XRootDStatus DoRm( FileSystem                      *fs,
                   Env                             *env,
                   const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc < 2 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  struct print_t
  {
    void print( const std::string &msg )
    {
      std::unique_lock<std::mutex> lck( mtx );
      std::cout << msg << '\n';
    }
    std::mutex mtx;
  };
  std::shared_ptr<print_t> print;
  if( argc - 1 > 0 )
    print = std::make_shared<print_t>();

  std::vector<Pipeline> rms;
  rms.reserve( argc - 1 );
  for( size_t i = 1; i < argc; ++i )
  {
    std::string fullPath;
    if( !BuildPath( fullPath, env, args[i] ).IsOK() )
    {
      log->Error( AppMsg, "Invalid path: %s", fullPath.c_str() );
      return XRootDStatus( stError, errInvalidArgs );
    }
    rms.emplace_back( Rm( fs, fullPath ) >>
                      [log, fullPath, print]( XRootDStatus &st )
                      {
                        if( !st.IsOK() )
                        {
                          log->Error( AppMsg, "Unable remove %s: %s",
                                              fullPath.c_str(),
                                              st.ToStr().c_str() );
                        }
                        if( print )
                        {
                          print->print( "rm " + fullPath + " : " + st.ToString() );
                        }
                      } );
  }

  //----------------------------------------------------------------------------
  // Run the query:
  // Parallel() will take the vector of Pipeline by reference and empty the
  // vector, so rms.size() will change after the call.
  //----------------------------------------------------------------------------
  const size_t rs = rms.size();
  XRootDStatus st = WaitFor( Parallel( rms ).AtLeast( rs ) );
  if( !st.IsOK() )
    return st;

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Truncate a file
//------------------------------------------------------------------------------
XRootDStatus DoTruncate( FileSystem                      *fs,
                         Env                             *env,
                         const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 3 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath;
  if( !BuildPath( fullPath, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  char *result;
  uint64_t size = ::strtoll( args[2].c_str(), &result, 0 );
  if( *result != 0 )
  {
    log->Error( AppMsg, "Size parameter needs to be an integer" );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  XRootDStatus st = fs->Truncate( fullPath, size );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable truncate %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Change the access rights to a file
//------------------------------------------------------------------------------
XRootDStatus DoChMod( FileSystem                      *fs,
                      Env                             *env,
                      const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 3 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath;
  if( !BuildPath( fullPath, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  Access::Mode mode = Access::None;
  XRootDStatus st = ConvertMode( mode, args[2] );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Invalid mode string." );
    return st;
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  st = fs->ChMod( fullPath, mode );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable change mode of %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Locate a path
//------------------------------------------------------------------------------
XRootDStatus DoLocate( FileSystem                      *fs,
                       Env                             *env,
                       const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc > 4 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  OpenFlags::Flags flags = OpenFlags::None;
  std::string path;
  bool        hasPath      = false;
  bool        doDeepLocate = false;
  for( uint32_t i = 1; i < argc; ++i )
  {
    if( args[i] == "-n" )
      flags |= OpenFlags::NoWait;
    else if( args[i] == "-r" )
      flags |= OpenFlags::Refresh;
    else if( args[i] == "-m" || args[i] == "-h" )
      flags |= OpenFlags::PrefName;
    else if( args[i] == "-i" )
      flags |= OpenFlags::Force;
    else if( args[i] == "-d" )
      doDeepLocate = true;
    else if( args[i] == "-p" )
    {
      Env *env = DefaultEnv::GetEnv();
      env->PutInt( "PreserveLocateTried", 0 );
    }
    else if( !hasPath )
    {
      path = args[i];
      hasPath = true;
    }
    else
    {
      log->Error( AppMsg, "Invalid argument: %s.", args[i].c_str() );
      return XRootDStatus( stError, errInvalidArgs );
    }
  }

  std::string fullPath;
  if( path[0] == '*' )
    fullPath = path;
  else
  {
    if( !BuildPath( fullPath, env, path ).IsOK() )
    {
      log->Error( AppMsg, "Invalid path." );
      return XRootDStatus( stError, errInvalidArgs );
    }
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  LocationInfo *info = 0;
  XRootDStatus  st;
  if( doDeepLocate )
    st = fs->DeepLocate( fullPath, flags, info );
  else
    st = fs->Locate( fullPath, flags, info );

  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable locate %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  //----------------------------------------------------------------------------
  // Print the result
  //----------------------------------------------------------------------------
  if( st.code == suPartial )
  {
    std::cerr << "[!] Some of the requests failed. The result may be ";
    std::cerr << "incomplete." << std::endl;
  }

  LocationInfo::Iterator it;
  for( it = info->Begin(); it != info->End(); ++it )
  {
    std::cout << it->GetAddress() << " ";
    switch( it->GetType() )
    {
      case LocationInfo::ManagerOnline:
        std::cout << "Manager ";
        break;
      case LocationInfo::ManagerPending:
        std::cout << "ManagerPending ";
        break;
      case LocationInfo::ServerOnline:
        std::cout << "Server ";
        break;
      case LocationInfo::ServerPending:
        std::cout << "ServerPending ";
        break;
      default:
        std::cout << "Unknown ";
    };

    switch( it->GetAccessType() )
    {
      case LocationInfo::Read:
        std::cout << "Read";
        break;
      case LocationInfo::ReadWrite:
        std::cout << "ReadWrite ";
        break;
      default:
        std::cout << "Unknown ";
    };
    std::cout << std::endl;
  }

  delete info;
  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Process stat query
//------------------------------------------------------------------------------
XRootDStatus ProcessStatQuery( StatInfo &info, const std::string &query )
{
  Log *log = DefaultEnv::GetLog();

  //----------------------------------------------------------------------------
  // Process the query
  //----------------------------------------------------------------------------
  bool isOrQuery = false;
  bool status    = true;
  if( query.find( '|' ) != std::string::npos )
  {
    isOrQuery = true;
    status    = false;
  }
  std::vector<std::string> queryFlags;
  if( isOrQuery )
    Utils::splitString( queryFlags, query, "|" );
  else
    Utils::splitString( queryFlags, query, "&" );

  //----------------------------------------------------------------------------
  // Initialize flag translation map and check the input flags
  //----------------------------------------------------------------------------
  std::map<std::string, StatInfo::Flags> flagMap;
  flagMap["XBitSet"]      = StatInfo::XBitSet;
  flagMap["IsDir"]        = StatInfo::IsDir;
  flagMap["Other"]        = StatInfo::Other;
  flagMap["Offline"]      = StatInfo::Offline;
  flagMap["POSCPending"]  = StatInfo::POSCPending;
  flagMap["IsReadable"]   = StatInfo::IsReadable;
  flagMap["IsWritable"]   = StatInfo::IsWritable;
  flagMap["BackUpExists"] = StatInfo::BackUpExists;

  std::vector<std::string>::iterator it;
  for( it = queryFlags.begin(); it != queryFlags.end(); ++it )
    if( flagMap.find( *it ) == flagMap.end() )
    {
      log->Error( AppMsg, "Flag '%s' is not recognized.", it->c_str() );
      return XRootDStatus( stError, errInvalidArgs );
    }

  //----------------------------------------------------------------------------
  // Process the query
  //----------------------------------------------------------------------------
  if( isOrQuery )
  {
    for( it = queryFlags.begin(); it != queryFlags.end(); ++it )
      if( info.TestFlags( flagMap[*it] ) )
        return XRootDStatus();
  }
  else
  {
    for( it = queryFlags.begin(); it != queryFlags.end(); ++it )
      if( !info.TestFlags( flagMap[*it] ) )
        return XRootDStatus( stError, errResponseNegative );
  }

  if( status )
    return XRootDStatus();
  return XRootDStatus( stError, errResponseNegative );
}

//------------------------------------------------------------------------------
// Stat a path
//------------------------------------------------------------------------------
XRootDStatus DoStat( FileSystem                      *fs,
                     Env                             *env,
                     const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc < 2 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::vector<std::string> paths;
  std::string query;

  for( uint32_t i = 1; i < args.size(); ++i )
  {
    if( args[i] == "-q" )
    {
      if( i < args.size()-1 )
      {
        query = args[i+1];
        ++i;
      }
      else
      {
        log->Error( AppMsg, "Parameter '-q' requires an argument." );
        return XRootDStatus( stError, errInvalidArgs );
      }
    }
    else
      paths.emplace_back( args[i] );
  }

  std::vector<XrdCl::Pipeline> stats;
  std::vector<std::tuple<std::future<StatInfo>, std::string>> results;
  for( auto &path : paths )
  {
    std::string fullPath;
    if( !BuildPath( fullPath, env, path ).IsOK() )
    {
      log->Error( AppMsg, "Invalid path." );
      return XRootDStatus( stError, errInvalidArgs );
    }
    std::future<XrdCl::StatInfo> ftr;
    stats.emplace_back( XrdCl::Stat( fs, fullPath ) >> ftr );
    results.emplace_back( std::move( ftr ), std::move( fullPath ) );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  XrdCl::Async( XrdCl::Parallel( stats ) );

  //----------------------------------------------------------------------------
  // Print the result
  //----------------------------------------------------------------------------
  XrdCl::XRootDStatus st;
  for( auto &tpl : results )
  {
    auto &ftr      = std::get<0>( tpl );
    auto &fullPath = std::get<1>( tpl );
    std::cout << std::endl;
    try
    {
      XrdCl::StatInfo info( ftr.get() );
      std::string flags;

      if( info.TestFlags( StatInfo::XBitSet ) )
        flags += "XBitSet|";
      if( info.TestFlags( StatInfo::IsDir ) )
        flags += "IsDir|";
      if( info.TestFlags( StatInfo::Other ) )
        flags += "Other|";
      if( info.TestFlags( StatInfo::Offline ) )
        flags += "Offline|";
      if( info.TestFlags( StatInfo::POSCPending ) )
        flags += "POSCPending|";
      if( info.TestFlags( StatInfo::IsReadable ) )
        flags += "IsReadable|";
      if( info.TestFlags( StatInfo::IsWritable ) )
        flags += "IsWritable|";
      if( info.TestFlags( StatInfo::BackUpExists ) )
        flags += "BackUpExists|";

      if( !flags.empty() )
        flags.erase( flags.length()-1, 1 );

      std::cout <<   "Path:   " << fullPath << std::endl;
      std::cout <<   "Id:     " << info.GetId() << std::endl;
      std::cout <<   "Size:   " << info.GetSize() << std::endl;
      std::cout <<   "MTime:  " << info.GetModTimeAsString() << std::endl;
      // if extended stat information is available we can print also
      // change time and access time
      if( info.ExtendedFormat() )
      {
        std::cout << "CTime:  " << info.GetChangeTimeAsString() << std::endl;
        std::cout << "ATime:  " << info.GetAccessTimeAsString() << std::endl;
      }
      std::cout << "Flags:  " << info.GetFlags() << " (" << flags << ")";

      // check if extended stat information is available
      if( info.ExtendedFormat() )
      {
        std::cout << "\nMode:   " << info.GetModeAsString() << std::endl;
        std::cout << "Owner:  " << info.GetOwner() << std::endl;
        std::cout << "Group:  " << info.GetGroup();
      }

      std::cout << std::endl;

      if( query.length() != 0 )
      {
        XRootDStatus s = ProcessStatQuery( info, query );
        if( !s.IsOK() )
          st = s;
        std::cout << "Query:  " << query << " " << std::endl;
      }
    }
    catch( XrdCl::PipelineException &ex )
    {
      st = ex.GetError();
      log->Error( AppMsg, "Unable stat %s: %s", fullPath.c_str(), st.ToStr().c_str() );
    }
  }

  return st;
}

//------------------------------------------------------------------------------
// Stat a VFS
//------------------------------------------------------------------------------
XRootDStatus DoStatVFS( FileSystem                      *fs,
                        Env                            *env,
                        const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 2 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string fullPath;
  if( !BuildPath( fullPath, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  StatInfoVFS *info = 0;
  XRootDStatus st = fs->StatVFS( fullPath, info );

  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable stat VFS at %s: %s",
                        fullPath.c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  //----------------------------------------------------------------------------
  // Print the result
  //----------------------------------------------------------------------------
  std::cout << "Path:                               ";
  std::cout << fullPath << std::endl;
  std::cout << "Nodes with RW space:                ";
  std::cout << info->GetNodesRW() << std::endl;
  std::cout << "Size of largest RW space (MB):      ";
  std::cout << info->GetFreeRW() << std::endl;
  std::cout << "Utilization of RW space (%):        ";
  std::cout << (uint16_t)info->GetUtilizationRW() << std::endl;
  std::cout << "Nodes with staging space:           ";
  std::cout << info->GetNodesStaging() << std::endl;
  std::cout << "Size of largest staging space (MB): ";
  std::cout << info->GetFreeStaging() << std::endl;
  std::cout << "Utilization of staging space (%):   ";
  std::cout << (uint16_t)info->GetUtilizationStaging() << std::endl;

  delete info;
  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Query the server
//------------------------------------------------------------------------------
XRootDStatus DoQuery( FileSystem                      *fs,
                      Env                             *env,
                      const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( !( argc >= 3 ) )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  QueryCode::Code qCode;
  if( args[1] == "config" )
    qCode = QueryCode::Config;
  else if( args[1] == "checksumcancel" )
    qCode = QueryCode::ChecksumCancel;
  else if( args[1] == "checksum" )
    qCode = QueryCode::Checksum;
  else if( args[1] == "opaque" )
    qCode = QueryCode::Opaque;
  else if( args[1] == "opaquefile" )
    qCode = QueryCode::OpaqueFile;
  else if( args[1] == "prepare" )
    qCode = QueryCode::Prepare;
  else if( args[1] == "space" )
    qCode = QueryCode::Space;
  else if( args[1] == "stats" )
    qCode = QueryCode::Stats;
  else if( args[1] == "xattr" )
    qCode = QueryCode::XAttr;
  else
  {
    log->Error( AppMsg, "Invalid query code." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  if( !( qCode & QueryCode::Prepare ) && argc != 3  )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string strArg = args[2];
  if( qCode & QueryCode::Prepare )
  {
    // strArg is supposed to contain already the request ID

    for( size_t i = 3; i < args.size(); ++i )
    {
      std::string path = args[i];
      if( !BuildPath( path, env, path ).IsOK() )
      {
        log->Error( AppMsg, "Invalid path." );
        return XRootDStatus( stError, errInvalidArgs );
      }
      // we use new line character as delimiter
      strArg += '\n';
      strArg += path;
    }
  }
  else
  {
    std::string strArg = args[2];
    if( qCode == QueryCode::ChecksumCancel ||
        qCode == QueryCode::Checksum       ||
        qCode == QueryCode::XAttr )
    {
      if( !BuildPath( strArg, env, args[2] ).IsOK() )
      {
        log->Error( AppMsg, "Invalid path." );
        return XRootDStatus( stError, errInvalidArgs );
      }
    }
  }

  //----------------------------------------------------------------------------
  // Run the query
  //----------------------------------------------------------------------------
  Buffer arg( strArg.size() );
  arg.FromString( strArg );
  Buffer *response = 0;
  XRootDStatus st = fs->Query( qCode, arg, response );

  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable run query %s: %s",
                        args[1].c_str(),
                        st.ToStr().c_str() );
    return st;
  }

  //----------------------------------------------------------------------------
  // Print the result
  //----------------------------------------------------------------------------
  std::cout << response->ToString() << std::endl;
  delete response;
  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Query the server
//------------------------------------------------------------------------------
XRootDStatus DoPrepare( FileSystem                      *fs,
                        Env                             *env,
                        const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc < 2 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  PrepareFlags::Flags      flags    = PrepareFlags::None;
  std::vector<std::string> files;
  uint8_t                  priority = 0;
  std::string              reqid;

  for( uint32_t i = 1; i < args.size(); ++i )
  {
    if( args[i] == "-p" )
    {
      if( i < args.size()-1 )
      {
        char *result;
        int32_t param = ::strtol( args[i+1].c_str(), &result, 0 );
        if( *result != 0 || param > 3 || param < 0 )
        {
          log->Error( AppMsg, "Size priotiry needs to be an integer between 0 "
                      "and 3" );
          return XRootDStatus( stError, errInvalidArgs );
        }
        priority = (uint8_t)param;
        ++i;
      }
      else
      {
        log->Error( AppMsg, "Parameter '-p' requires an argument." );
        return XRootDStatus( stError, errInvalidArgs );
      }
    }
    else if( args[i] == "-c" )
      flags |= PrepareFlags::Colocate;
    else if( args[i] == "-f" )
      flags |= PrepareFlags::Fresh;
    else if( args[i] == "-s" )
      flags |= PrepareFlags::Stage;
    else if( args[i] == "-w" )
      flags |= PrepareFlags::WriteMode;
    else if( args[i] == "-e" )
      flags |= PrepareFlags::Evict;
    else if( args[i] == "-a" )
    {
      flags |= PrepareFlags::Cancel;
      if( i < args.size()-1 )
      {
        // by convention the request ID appears as the the first token
        // in the list of files
        files.push_back( args[i+1] );
        ++i;
      }
      else
      {
        log->Error( AppMsg, "Parameter '-a' requires an argument." );
        return XRootDStatus( stError, errInvalidArgs );
      }
    }
    else
      files.push_back( args[i] );
  }

  if( files.empty() )
  {
    log->Error( AppMsg, "Filename missing." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Run the command
  //----------------------------------------------------------------------------
  Buffer *response = 0;
  XRootDStatus st = fs->Prepare( files, flags, priority, response );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Prepare request failed: %s", st.ToStr().c_str() );
    return st;
  }

  if( ( flags & PrepareFlags::Stage ) && response )
  {
    std::cout << response->ToString() << '\n';
  }

  delete response;
  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Copy progress handler
//------------------------------------------------------------------------------
class ProgressDisplay: public XrdCl::CopyProgressHandler
{
  public:
    //--------------------------------------------------------------------------
    // Constructor
    //--------------------------------------------------------------------------
    ProgressDisplay(): pBytesProcessed(0), pBytesTotal(0), pPrevious(0)
    {}

    //--------------------------------------------------------------------------
    // End job
    //--------------------------------------------------------------------------
    virtual void EndJob( uint32_t jobNum, const XrdCl::PropertyList *results )
    {
      JobProgress( jobNum, pBytesProcessed, pBytesTotal );
      std::cerr << std::endl;
    }

    //--------------------------------------------------------------------------
    // Job progress
    //--------------------------------------------------------------------------
    virtual void JobProgress( uint32_t jobNum,
                              uint64_t bytesProcessed,
                              uint64_t bytesTotal )
    {
      pBytesProcessed = bytesProcessed;
      pBytesTotal     = bytesTotal;

      time_t now = time(0);
      if( (now - pPrevious < 1) && (bytesProcessed != bytesTotal) )
        return;
      pPrevious = now;

      std::cerr << "\r";
      std::cerr << "Progress: ";
      std::cerr << XrdCl::Utils::BytesToString(bytesProcessed) << "B ";

      if( bytesTotal )
        std::cerr << "(" << bytesProcessed*100/bytesTotal << "%)";

      std::cerr << std::flush;
     }

  private:
    uint64_t          pBytesProcessed;
    uint64_t          pBytesTotal;
    time_t            pPrevious;
};

//------------------------------------------------------------------------------
// Cat a file
//------------------------------------------------------------------------------
XRootDStatus DoCat( FileSystem                      *fs,
                    Env                             *env,
                    const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc < 2 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string server;
  env->GetString( "ServerURL", server );
  if( server.empty() )
  {
    log->Error( AppMsg, "Invalid address: \"%s\".", server.c_str() );
    return XRootDStatus( stError, errInvalidAddr );
  }

  std::vector<std::string> remotes;
  std::string local;

  for( uint32_t i = 1; i < args.size(); ++i )
  {
    if( args[i] == "-o" )
    {
      if( i < args.size()-1 )
      {
        local = args[i+1];
        ++i;
      }
      else
      {
        log->Error( AppMsg, "Parameter '-o' requires an argument." );
        return XRootDStatus( stError, errInvalidArgs );
      }
    }
    else
      remotes.emplace_back( args[i] );
  }

  if( !local.empty() && remotes.size() > 1 )
  {
    log->Error( AppMsg, "If '-o' is used only can be used with only one remote file." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::vector<URL> remoteUrls;
  remoteUrls.reserve( remotes.size() );
  for( auto &remote : remotes )
  {
    std::string remoteFile;
    if( !BuildPath( remoteFile, env, remote ).IsOK() )
    {
      log->Error( AppMsg, "Invalid path." );
      return XRootDStatus( stError, errInvalidArgs );
    }

    remoteUrls.emplace_back( server );
    remoteUrls.back().SetPath( remoteFile );
  }

  //----------------------------------------------------------------------------
  // Fetch the data
  //----------------------------------------------------------------------------
  CopyProgressHandler *handler = 0; ProgressDisplay d;
  CopyProcess process;
  std::vector<PropertyList> props( remoteUrls.size() ), results( remoteUrls.size() );

  for( size_t i = 0; i < remoteUrls.size(); ++i )
  {
    props[i].Set( "source", remoteUrls[i].GetURL() );
    if( !local.empty() )
    {
      props[i].Set( "target", std::string( "file://" ) + local );
      handler = &d;
    }
    else
      props[i].Set( "target", "stdio://-" );

    props[i].Set( "dynamicSource", true );

    XRootDStatus st = process.AddJob( props[i], &results[i] );
    if( !st.IsOK() )
    {
      log->Error( AppMsg, "Job adding failed: %s.", st.ToStr().c_str() );
      return st;
    }
  }

  XRootDStatus st = process.Prepare();
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Copy preparation failed: %s.", st.ToStr().c_str() );
    return st;
  }

  st = process.Run(handler);
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Cope process failed: %s.", st.ToStr().c_str() );
    return st;
  }

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Tail a file
//------------------------------------------------------------------------------
XRootDStatus DoTail( FileSystem                      *fs,
                     Env                             *env,
                     const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc < 2 || argc > 5 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string server;
  env->GetString( "ServerURL", server );
  if( server.empty() )
  {
    log->Error( AppMsg, "Invalid address: \"%s\".", server.c_str() );
    return XRootDStatus( stError, errInvalidAddr );
  }

  std::string remote;
  bool        followMode = false;
  uint32_t    offset     = 512;

  for( uint32_t i = 1; i < args.size(); ++i )
  {
    if( args[i] == "-f" )
      followMode = true;
    else if( args[i] == "-c" )
    {
      if( i < args.size()-1 )
      {
        char *result;
        offset = ::strtol( args[i+1].c_str(), &result, 0 );
        if( *result != 0 )
        {
          log->Error( AppMsg, "Offset from the end needs to be a number: %s",
                      args[i+1].c_str() );
          return XRootDStatus( stError, errInvalidArgs );
        }
        ++i;
      }
      else
      {
        log->Error( AppMsg, "Parameter '-n' requires an argument." );
        return XRootDStatus( stError, errInvalidArgs );
      }
    }
    else
      remote = args[i];
  }

  std::string remoteFile;
  if( !BuildPath( remoteFile, env, remote ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  URL remoteUrl( server );
  remoteUrl.SetPath( remoteFile );

  //----------------------------------------------------------------------------
  // Fetch the data
  //----------------------------------------------------------------------------
  File file;
  XRootDStatus st = file.Open( remoteUrl.GetURL(), OpenFlags::Read );
  if( !st.IsOK() )
  {
    log->Error( AppMsg, "Unable to open file %s: %s",
                remoteUrl.GetObfuscatedURL().c_str(), st.ToStr().c_str() );
    return st;
  }

  StatInfo *info = 0;
  uint64_t size = 0;
  st = file.Stat( false, info );
  if (st.IsOK()) size = info->GetSize();

  if( size < offset )
    offset = 0;
  else
    offset = size - offset;

  uint32_t  chunkSize = 1*1024*1024;
  char     *buffer    = new char[chunkSize];
  uint32_t  bytesRead = 0;
  while(1)
  {
    st = file.Read( offset, chunkSize, buffer, bytesRead );
    if( !st.IsOK() )
    {
      log->Error( AppMsg, "Unable to read from %s: %s",
                  remoteUrl.GetObfuscatedURL().c_str(), st.ToStr().c_str() );
      delete [] buffer;
      return st;
    }

    offset += bytesRead;
    int ret = write( 1, buffer, bytesRead );
    if( ret == -1 )
    {
      log->Error( AppMsg, "Unable to write to stdout: %s",
                  XrdSysE2T(errno) );
      delete [] buffer;
      return st;
    }

    if( bytesRead < chunkSize )
    {
      if( !followMode )
        break;
      sleep(1);
    }
  }
  delete [] buffer;

  XRootDStatus stC = file.Close();

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Print statistics concerning given space
//------------------------------------------------------------------------------
XRootDStatus DoSpaceInfo( FileSystem                      *fs,
                          Env                             *env,
                          const FSExecutor::CommandParams &args )
{
  using namespace XrdCl;

  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc != 2 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  FileSystemUtils::SpaceInfo *i = 0;

  XRootDStatus st = FileSystemUtils::GetSpaceInfo( i, fs, args[1] );
  if( !st.IsOK() )
    return st;

  if( st.code == suPartial )
  {
    std::cerr << "[!] Some of the requests failed. The result may be ";
    std::cerr << "incomplete." << std::endl;
  }

  std::cout << "Path:               " << args[1]                  << std::endl;
  std::cout << "Total:              " << i->GetTotal()            << std::endl;
  std::cout << "Free:               " << i->GetFree()             << std::endl;
  std::cout << "Used:               " << i->GetUsed()             << std::endl;
  std::cout << "Largest free chunk: " << i->GetLargestFreeChunk() << std::endl;

  delete i;
  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Carry out xattr operation
//------------------------------------------------------------------------------
XRootDStatus DoXAttr( FileSystem                      *fs,
                      Env                             *env,
                      const FSExecutor::CommandParams &args )
{
  //----------------------------------------------------------------------------
  // Check up the args
  //----------------------------------------------------------------------------
  Log         *log     = DefaultEnv::GetLog();
  uint32_t     argc    = args.size();

  if( argc < 3 )
  {
    log->Error( AppMsg, "Wrong number of arguments." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  kXR_char code = 0;
  if( args[2] == "set")
    code = kXR_fattrSet;
  else if( args[2] == "get" )
    code = kXR_fattrGet;
  else if( args[2] == "del" )
    code = kXR_fattrDel;
  else if( args[2] == "list" )
    code = kXR_fattrList;
  else
  {
    log->Error( AppMsg, "Invalid xattr code." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  std::string path;
  if( !BuildPath( path, env, args[1] ).IsOK() )
  {
    log->Error( AppMsg, "Invalid path." );
    return XRootDStatus( stError, errInvalidArgs );
  }

  //----------------------------------------------------------------------------
  // Issue the xattr operation
  //----------------------------------------------------------------------------
  XRootDStatus status;
  switch( code )
  {
    case kXR_fattrSet:
    {
      if( argc != 4 )
      {
        log->Error( AppMsg, "Wrong number of arguments." );
        return XRootDStatus( stError, errInvalidArgs );
      }

      std::string key_value = args[3];
      size_t pos = key_value.find( '=' );
      std::string key   = key_value.substr( 0, pos );
      std::string value = key_value.substr( pos + 1 );
      std::vector<xattr_t> attrs;
      attrs.push_back( std::make_tuple( key, value ) );

      std::vector<XAttrStatus> result;
      XRootDStatus status = fs->SetXAttr( path, attrs, result );
      XAttrStatus xst = status.IsOK() ? result.front() : XAttrStatus( key, status );

      if( !xst.status.IsOK() )
        status = xst.status;

      if( !status.IsOK() )
        log->Error( AppMsg, "Unable to xattr set %s %s: %s",
                            key.c_str(), value.c_str(),
                            status.ToStr().c_str() );
      return status;
    }

    case kXR_fattrGet:
    {
      if( argc != 4 )
      {
        log->Error( AppMsg, "Wrong number of arguments." );
        return XRootDStatus( stError, errInvalidArgs );
      }

      std::string key = args[3];
      std::vector<std::string> attrs;
      attrs.push_back( key );

      std::vector<XAttr> result;
      XRootDStatus status = fs->GetXAttr( path, attrs, result );
      XAttr xattr = status.IsOK() ? result.front() : XAttr( key, status );

      if( !xattr.status.IsOK() )
        status = xattr.status;

      if( !status.IsOK() )
        log->Error( AppMsg, "Unable to xattr get %s : %s",
                            key.c_str(),
                            status.ToStr().c_str() );
      else
      {
        std::cout << "# file: " << path << '\n';
        std::cout << xattr.name << "=\"" << xattr.value << "\"\n";
      }

      return status;
    }

    case kXR_fattrDel:
    {
      if( argc != 4 )
      {
        log->Error( AppMsg, "Wrong number of arguments." );
        return XRootDStatus( stError, errInvalidArgs );
      }

      std::string key = args[3];
      std::vector<std::string> attrs;
      attrs.push_back( key );

      std::vector<XAttrStatus> result ;
      XRootDStatus status = fs->DelXAttr( path, attrs, result );
      XAttrStatus xst = status.IsOK() ? result.front() : XAttrStatus( key, status );

      if( !xst.status.IsOK() )
        status = xst.status;

      if( !status.IsOK() )
        log->Error( AppMsg, "Unable to xattr del %s : %s",
                            key.c_str(),
                            status.ToStr().c_str() );
      return status;
    }

    case kXR_fattrList:
    {
      if( argc != 3 )
      {
        log->Error( AppMsg, "Wrong number of arguments." );
        return XRootDStatus( stError, errInvalidArgs );
      }

      std::vector<XAttr> result;
      XRootDStatus status = fs->ListXAttr( path, result );

      if( !status.IsOK() )
        log->Error( AppMsg, "Unable to xattr list : %s",
                            status.ToStr().c_str() );
      else
      {
        std::cout << "# file: " << path << '\n';
        auto itr = result.begin();
        for( ; itr != result.end(); ++itr )
          std::cout << itr->name << "=\"" << itr->value << "\"\n";
      }

      return status;
    }

    default:
      return XRootDStatus( stError, errInvalidAddr );
  }
}

//------------------------------------------------------------------------------
// Print help
//------------------------------------------------------------------------------
XRootDStatus PrintHelp( FileSystem *, Env *,
                        const FSExecutor::CommandParams & )
{
  printf( "Usage:\n"                                                          );
  printf( "   xrdfs [--no-cwd] host[:port]              - interactive mode\n" );
  printf( "   xrdfs            host[:port] command args - batch mode\n\n"     );

  printf( "Available options:\n\n"                                            );

  printf( "   --no-cwd no CWD is being preset\n\n"                            );

  printf( "Available commands:\n\n"                                           );

  printf( "   exit\n"                                                         );
  printf( "     Exits from the program.\n\n"                                  );

  printf( "   help\n"                                                         );
  printf( "     This help screen.\n\n"                                        );

  printf( "   cache {evict | fevict} <path>\n"                                );
  printf( "     Evict a file from a cache if not in use; while fevict\n"      );
  printf( "     forcibly evicts the file causing any current uses of the\n"   );
  printf( "     file to get read failures on a subsequent read\n\n"           );

  printf( "   cd <path>\n"                                                    );
  printf( "     Change the current working directory\n\n"                     );

  printf( "   chmod <path> <user><group><other>\n"                            );
  printf( "     Modify permissions. Permission string example:\n"             );
  printf( "     rwxr-x--x\n\n"                                                );

  printf( "   ls [-l] [-u] [-R] [-D] [-Z] [-C] [dirname]\n"                   );
  printf( "     Get directory listing.\n"                                     );
  printf( "     -l stat every entry and print long listing\n"                 );
  printf( "     -u print paths as URLs\n"                                     );
  printf( "     -R list subdirectories recursively\n"                         );
  printf( "     -D show duplicate entries"                                    );
  printf( "     -Z if a ZIP archive list its content\n"                       );
  printf( "     -C checksum every entry\n\n"                                  );

  printf( "   locate [-n] [-r] [-d] [-m] [-i] [-p] <path>\n"                  );
  printf( "     Get the locations of the path.\n"                             );
  printf( "     -r refresh, don't use cached locations\n"                     );
  printf( "     -n make the server return the response immediately even\n"    );
  printf( "        though it may be incomplete\n"                             );
  printf( "     -d do a recursive (deep) locate\n"                            );
  printf( "     -m|-h prefer host names to IP addresses\n"                    );
  printf( "     -i ignore network dependencies\n"                             );
  printf( "     -p be passive: ignore tried/triedrc cgi opaque info\n\n"      );

  printf( "   mkdir [-p] [-m<user><group><other>] <dirname>\n"                );
  printf( "     Creates a directory/tree of directories.\n\n"                 );

  printf( "   mv <path1> <path2>\n"                                           );
  printf( "     Move path1 to path2 locally on the same server.\n\n"          );

  printf( "   stat [-q query] <path>\n"                                       );
  printf( "     Get info about the file or directory.\n"                      );
  printf( "     -q query optional flag query parameter that makes\n"          );
  printf( "              xrdfs return error code to the shell if the\n"       );
  printf( "              requested flag combination is not present;\n"        );
  printf( "              flags may be combined together using '|' or '&'\n"   );
  printf( "              Available flags:\n"                                  );
  printf( "              XBitSet, IsDir, Other, Offline, POSCPending,\n"      );
  printf( "              IsReadable, IsWritable\n\n"                          );

  printf( "   statvfs <path>\n"                                               );
  printf( "     Get info about a virtual file system.\n\n"                    );

  printf( "   query <code> <parameters>\n"                                    );
  printf( "     Obtain server information. Query codes:\n\n"                  );

  printf( "     config         <what>   Server configuration; <what> is\n"    );
  printf( "                             one of the following:\n"              );
  printf( "                               bind_max      - the maximum number of parallel streams\n"  );
  printf( "                               chksum        - the supported checksum\n"                  );
  printf( "                               cms           - the status of the cmsd\n"                  );
  printf( "                               pio_max       - maximum number of parallel I/O requests\n" );
  printf( "                               readv_ior_max - maximum size of a readv element\n"         );
  printf( "                               readv_iov_max - maximum number of readv entries\n"         );
  printf( "                               role          - the role in a cluster\n"                   );
  printf( "                               sitename      - the site name\n"                           );
  printf( "                               tpc           - support for third party copies\n"          );
  printf( "                               version       - the version of the server\n"               );
  printf( "                               wan_port      - the port to use for wan copies\n"          );
  printf( "                               wan_window    - the wan_port window size\n"                );
  printf( "                               window        - the tcp window size\n"                     );
  printf( "     checksumcancel <path>   File checksum cancellation\n"       );
  printf( "     checksum       <path>   File checksum\n"                    );
  printf( "     opaque         <arg>    Implementation dependent\n"         );
  printf( "     opaquefile     <arg>    Implementation dependent\n"         );
  printf( "     space          <space>  Logical space stats\n"              );
  printf( "     stats          <what>   Server stats; <what> is a list\n"   );
  printf( "                             of letters indicating information\n");
  printf( "                             to be returned:\n"                  );
  printf( "                               a - all statistics\n"             );
  printf( "                               p - protocol statistics\n"        );
  printf( "                               b - buffer usage statistics\n"    );
  printf( "                               s - scheduling statistics\n"      );
  printf( "                               d - device polling statistics\n"  );
  printf( "                               u - usage statistics\n"           );
  printf( "                               i - server identification\n"      );
  printf( "                               z - synchronized statistics\n"    );
  printf( "                               l - connection statistics\n"      );
  printf( "     xattr          <path>   Extended attributes\n"            );
  printf( "     prepare        <reqid> [filenames]  Prepare request status\n\n" );

  printf( "   rm <filename>\n"                                              );
  printf( "     Remove a file.\n\n"                                         );

  printf( "   rmdir <dirname>\n"                                            );
  printf( "     Remove a directory.\n\n"                                    );

  printf( "   truncate <filename> <length>\n"                               );
  printf( "     Truncate a file.\n\n"                                       );

  printf( "   prepare [-c] [-f] [-s] [-w] [-e] [-p priority] [-a requestid] filenames\n"   );
  printf( "     Prepare one or more files for access.\n"                    );
  printf( "     -c co-locate staged files if possible\n"                    );
  printf( "     -f refresh file access time even if the location is known\n" );
  printf( "     -s stage the files to disk if they are not online\n"        );
  printf( "     -w the files will be accessed for modification\n"           );
  printf( "     -p priority of the request, 0 (lowest) - 3 (highest)\n"     );
  printf( "     -a abort stage request\n"                                   );
  printf( "     -e evict the file from disk cache\n\n"                      );

  printf( "   cat [-o local file] files\n"                                  );
  printf( "     Print contents of one or more files to stdout.\n"           );
  printf( "     -o print to the specified local file\n\n"                   );

  printf( "   tail [-c bytes] [-f] file\n"                                  );
  printf( "     Output last part of files to stdout.\n"                     );
  printf( "     -c num_bytes out last num_bytes\n"                          );
  printf( "     -f           output appended data as file grows\n\n"        );

  printf( "   spaceinfo path\n"                                             );
  printf( "     Get space statistics for given path.\n\n"                   );

  printf( "   xattr <path> <code> <params> \n"                              );
  printf( "     Operation on extended attributes. Codes:\n\n"               );
  printf( "     set   <attr>          Set extended attribute; <attr> is\n"  );
  printf( "                             string of form name=value\n"        );
  printf( "     get   <name>          Get extended attribute\n"             );
  printf( "     del   <name>          Delete extended attribute\n"          );
  printf( "     list                  List extended attributes\n\n"         );

  return XRootDStatus();
}

//------------------------------------------------------------------------------
// Create the executor object
//------------------------------------------------------------------------------
FSExecutor *CreateExecutor( const URL &url )
{
  Env *env = new Env();
  env->PutString( "CWD", "/" );
  FSExecutor *executor = new FSExecutor( url, env );
  executor->AddCommand( "cache",       DoCache      );
  executor->AddCommand( "cd",          DoCD         );
  executor->AddCommand( "chmod",       DoChMod      );
  executor->AddCommand( "ls",          DoLS         );
  executor->AddCommand( "help",        PrintHelp    );
  executor->AddCommand( "stat",        DoStat       );
  executor->AddCommand( "statvfs",     DoStatVFS    );
  executor->AddCommand( "locate",      DoLocate     );
  executor->AddCommand( "mv",          DoMv         );
  executor->AddCommand( "mkdir",       DoMkDir      );
  executor->AddCommand( "rm",          DoRm         );
  executor->AddCommand( "rmdir",       DoRmDir      );
  executor->AddCommand( "query",       DoQuery      );
  executor->AddCommand( "truncate",    DoTruncate   );
  executor->AddCommand( "prepare",     DoPrepare    );
  executor->AddCommand( "cat",         DoCat        );
  executor->AddCommand( "tail",        DoTail       );
  executor->AddCommand( "spaceinfo",   DoSpaceInfo  );
  executor->AddCommand( "xattr",       DoXAttr      );
  return executor;
}

//------------------------------------------------------------------------------
// Execute command
//------------------------------------------------------------------------------
int ExecuteCommand( FSExecutor *ex, int argc, char **argv )
{
  // std::vector<std::string> args (argv, argv + argc);
  std::vector<std::string> args;
  args.reserve(argc);
  for (int i = 0; i < argc; ++i)
  {
    args.push_back(argv[i]);
  }
  XRootDStatus st = ex->Execute( args );
  if( !st.IsOK() )
    std::cerr << st.ToStr() << std::endl;
  return st.GetShellCode();
}

//------------------------------------------------------------------------------
// Define some functions required to function when build without readline
//------------------------------------------------------------------------------
#ifndef HAVE_READLINE
char *readline(const char *prompt)
{
    std::cout << prompt << std::flush;
    std::string input;
    std::getline( std::cin, input );

    if( !std::cin.good() )
        return 0;

    char *linebuf = (char *)malloc( input.size()+1 );
    strncpy( linebuf, input.c_str(), input.size()+1 );

    return linebuf;
}

void add_history( const char * )
{
}

void rl_bind_key( char, uint16_t )
{
}

uint16_t rl_insert = 0;

int read_history( const char * )
{
  return 0;
}

int write_history( const char * )
{
  return 0;
}
#endif

//------------------------------------------------------------------------------
// Build the command prompt
//------------------------------------------------------------------------------
std::string BuildPrompt( Env *env, const URL &url )
{
  std::ostringstream prompt;
  std::string cwd = "/";
  env->GetString( "CWD", cwd );
  prompt << "[" << url.GetHostId() << "] " << cwd << " > ";
  return prompt.str();
}

//------------------------------------------------------------------------
//! parse command line
//!
//! @ result : command parameters
//! @ input  : string containing the command line
//! @ return : true if the command has been completed, false otherwise
//------------------------------------------------------------------------
bool getArguments (std::vector<std::string> & result, const std::string &input)
{
  // the delimiter (space in the case of command line)
  static const char delimiter = ' ';
  // two types of quotes: single and double quotes
  const char singleQuote = '\'', doubleQuote = '\"';
  // if the current character of the command has been
  // quoted 'currentQuote' holds the type of quote,
  // otherwise it holds the null character
  char currentQuote = '\0';

  std::string tmp;
  for (std::string::const_iterator it = input.begin (); it != input.end (); ++it)
  {
    // if we encountered a quote character ...
    if (*it == singleQuote || *it == doubleQuote)
    {
      // if we are not within quoted text ...
      if (!currentQuote)
      {
        currentQuote = *it; // set the type of quote
        continue; // and continue, the quote character itself is not a part of the parameter
      }
      // otherwise if it is the closing quote character ...
      else if (currentQuote == *it)
      {
        currentQuote = '\0'; // reset the current quote type
        continue; // and continue, the quote character itself is not a part of the parameter
      }
    }
    // if we are within quoted text or the character is not a delimiter ...
    if (currentQuote || *it != delimiter)
      {
        // concatenate it
        tmp += *it;
      }
    else
      {
        // otherwise add a parameter and erase the tmp string
        if (!tmp.empty ())
        {
          result.push_back(tmp);
          tmp.erase ();
        }
      }
    }
  // if the there are some remainders of the command add them
  if (!tmp.empty())
  {
    result.push_back(tmp);
  }
  // return true if the quotation has been closed
  return currentQuote == '\0';
}

//------------------------------------------------------------------------------
// Execute interactive
//------------------------------------------------------------------------------
int ExecuteInteractive( const URL &url, bool noCwd = false )
{
  //----------------------------------------------------------------------------
  // Set up the environment
  //----------------------------------------------------------------------------
  std::string historyFile = getenv( "HOME" );
  historyFile += "/.xrdquery.history";
  rl_bind_key( '\t', rl_insert );
  read_history( historyFile.c_str() );
  FSExecutor *ex = CreateExecutor( url );

  if( noCwd )
    ex->GetEnv()->PutInt( "NoCWD", 1 );

  //----------------------------------------------------------------------------
  // Execute the commands
  //----------------------------------------------------------------------------
  std::string cmdline;
  while(1)
  {
    char *linebuf = 0;
    // print new prompt only if the previous line was complete
    // (a line is considered not to be complete if a quote has
    // been opened but it has not been closed)
    linebuf = readline( cmdline.empty() ? BuildPrompt( ex->GetEnv(), url ).c_str() : "> " );
    if( !linebuf || !strncmp( linebuf, "exit", 4 ) || !strncmp( linebuf, "quit", 4 ) )
    {
      std::cout << "Goodbye." << std::endl << std::endl;
      break;
    }
    if( !*linebuf)
    {
      free( linebuf );
      continue;
    }
    std::vector<std::string> args;
    cmdline += linebuf;
    free( linebuf );
    if (getArguments( args, cmdline ))
    {
      XRootDStatus st = ex->Execute( args );
      add_history( cmdline.c_str() );
      cmdline.erase();
      if( !st.IsOK() )
        std::cerr << st.ToStr() << std::endl;
    }
  }

  //----------------------------------------------------------------------------
  // Cleanup
  //----------------------------------------------------------------------------
  delete ex;
  write_history( historyFile.c_str() );
  return 0;
}

//------------------------------------------------------------------------------
// Execute command
//------------------------------------------------------------------------------
int ExecuteCommand( const URL &url, int argc, char **argv )
{
  //----------------------------------------------------------------------------
  // Build the command to be executed
  //----------------------------------------------------------------------------
  std::string commandline;
  for( int i = 0; i < argc; ++i )
  {
    commandline += argv[i];
    commandline += " ";
  }

  FSExecutor *ex = CreateExecutor( url );
  ex->GetEnv()->PutInt( "NoCWD", 1 );
  int st = ExecuteCommand( ex, argc, argv );
  delete ex;
  return st;
}

//------------------------------------------------------------------------------
// Start the show
//------------------------------------------------------------------------------
int main( int argc, char **argv )
{
  //----------------------------------------------------------------------------
  // Check the commandline parameters
  //----------------------------------------------------------------------------
  XrdCl::FSExecutor::CommandParams params;
  if( argc == 1 )
  {
    PrintHelp( 0, 0, params );
    return 1;
  }

  if( !strcmp( argv[1], "--help" ) ||
      !strcmp( argv[1], "-h" ) )
  {
    PrintHelp( 0, 0, params );
    return 0;
  }

  bool noCwd = false;
  int urlIndex = 1;
  if( !strcmp( argv[1], "--no-cwd") )
  {
    ++urlIndex;
    noCwd = true;
  }

  URL url( argv[urlIndex] );
  if( !url.IsValid() )
  {
    PrintHelp( 0, 0, params );
    return 1;
  }

  if( argc == urlIndex + 1 )
    return ExecuteInteractive( url, noCwd );
  int shift = urlIndex + 1;
  return ExecuteCommand( url, argc-shift, argv+shift );
}
