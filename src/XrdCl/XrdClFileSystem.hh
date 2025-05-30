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

#ifndef __XRD_CL_FILE_SYSTEM_HH__
#define __XRD_CL_FILE_SYSTEM_HH__

#include "XrdCl/XrdClURL.hh"
#include "XrdCl/XrdClStatus.hh"
#include "XrdOuc/XrdOucEnum.hh"
#include "XrdOuc/XrdOucCompiler.hh"
#include "XrdCl/XrdClXRootDResponses.hh"
#include "XrdSys/XrdSysPthread.hh"
#include "XProtocol/XProtocol.hh"
#include <string>
#include <vector>

namespace XrdCl
{
  class PostMaster;
  class FileSystemPlugIn;
  struct MessageSendParams;

  //----------------------------------------------------------------------------
  //! XRootD query request codes
  //----------------------------------------------------------------------------
  struct QueryCode
  {
    //--------------------------------------------------------------------------
    //! XRootD query request codes
    //--------------------------------------------------------------------------
    enum Code
    {
      Config         = kXR_Qconfig,    //!< Query server configuration
      ChecksumCancel = kXR_Qckscan,    //!< Query file checksum cancellation
      Checksum       = kXR_Qcksum,     //!< Query file checksum
      Opaque         = kXR_Qopaque,    //!< Implementation dependent
      OpaqueFile     = kXR_Qopaquf,    //!< Implementation dependent
      Prepare        = kXR_QPrep,      //!< Query prepare status
      Space          = kXR_Qspace,     //!< Query logical space stats
      Stats          = kXR_QStats,     //!< Query server stats
      Visa           = kXR_Qvisa,      //!< Query file visa attributes
      XAttr          = kXR_Qxattr      //!< Query file extended attributes
    };
  };

  //----------------------------------------------------------------------------
  //! Open flags, may be or'd when appropriate
  //----------------------------------------------------------------------------
  struct OpenFlags
  {
    //--------------------------------------------------------------------------
    //! Open flags, may be or'd when appropriate
    //--------------------------------------------------------------------------
    enum Flags
    {
      None     = 0,                 //!< Nothing
      Compress = kXR_compress,      //!< Read compressed data for open (ignored),
                                    //!< for kXR_locate return unique hosts
      Delete   = kXR_delete,        //!< Open a new file, deleting any existing
                                    //!< file
      Force    = kXR_force,         //!< Ignore file usage rules, for kXR_locate
                                    //!< it means ignoreing network dependencies
      MakePath = kXR_mkpath,        //!< Create directory path if it does not
                                    //!< already exist
      New      = kXR_new,           //!< Open the file only if it does not already
                                    //!< exist
      NoWait   = kXR_nowait,        //!< Open the file only if it does not cause
                                    //!< a wait. For locate: provide a location as
                                    //!< soon as one becomes known. This means
                                    //!< that not all locations are necessarily
                                    //!< returned. If the file does not exist a
                                    //!< wait is still imposed.
//      Append   = kXR_open_apnd,   //!< Open only for appending
      Read     = kXR_open_read,     //!< Open only for reading
      Update   = kXR_open_updt,     //!< Open for reading and writing
      Write    = kXR_open_wrto,     //!< Open only for writing
      POSC     = kXR_posc,          //!< Enable Persist On Successful Close
                                    //!< processing
      Refresh  = kXR_refresh,       //!< Refresh the cached information on file's
                                    //!< location. Voids NoWait.
      Replica  = kXR_replica,       //!< The file is being opened for replica
                                    //!< creation
      SeqIO    = kXR_seqio,         //!< File will be read or written sequentially
      PrefName = kXR_prefname,      //!< Hostname response is prefered, applies
                                    //!< only to FileSystem::Locate
      IntentDirList = kXR_4dirlist  //!< Make sure the server knows we are doing
                                    //!< locate in context of a dir list operation
    };
  };
  XRDOUC_ENUM_OPERATORS( OpenFlags::Flags )

  //----------------------------------------------------------------------------
  //! Access mode
  //----------------------------------------------------------------------------
  struct Access
  {
    //--------------------------------------------------------------------------
    //! Access mode
    //--------------------------------------------------------------------------
    enum Mode
    {
      None = 0,
      UR   = kXR_ur,         //!< owner readable
      UW   = kXR_uw,         //!< owner writable
      UX   = kXR_ux,         //!< owner executable/browsable
      GR   = kXR_gr,         //!< group readable
      GW   = kXR_gw,         //!< group writable
      GX   = kXR_gx,         //!< group executable/browsable
      OR   = kXR_or,         //!< world readable
      OW   = kXR_ow,         //!< world writeable
      OX   = kXR_ox          //!< world executable/browsable
    };
  };
  XRDOUC_ENUM_OPERATORS( Access::Mode )

  //----------------------------------------------------------------------------
  //! MkDir flags
  //----------------------------------------------------------------------------
  struct MkDirFlags
  {
    enum Flags
    {
      None     = 0,  //!< Nothing special
      MakePath = 1   //!< create the entire directory tree if it doesn't exist
    };
  };
  XRDOUC_ENUM_OPERATORS( MkDirFlags::Flags )

  //----------------------------------------------------------------------------
  //! DirList flags
  //----------------------------------------------------------------------------
  struct DirListFlags
  {
    enum Flags
    {
      None      = 0,  //!< Nothing special
      Stat      = 1,  //!< Stat each entry
      Locate    = 2,  //!< Locate all servers hosting the directory and send
                      //!< the dirlist request to all of them
      Recursive = 4,  //!< Do a recursive listing
      Merge     = 8,  //!< Merge duplicates
      Chunked   = 16, //!< Serve chunked results for better performance
      Zip       = 32, //!< List content of ZIP files
      Cksm      = 64  //!< Get checksum for every entry
    };
  };
  XRDOUC_ENUM_OPERATORS( DirListFlags::Flags )

  //----------------------------------------------------------------------------
  //! Prepare flags
  //----------------------------------------------------------------------------
  struct PrepareFlags
  {
    enum Flags
    {
      None        = 0,              //!< no flags
      Colocate    = kXR_coloc,      //!< co-locate staged files, if possible
      Fresh       = kXR_fresh,      //!< refresh file access time even if
                                    //!< the location is known
      Stage       = kXR_stage,      //!< stage the file to disk if it is not
                                    //!< online
      WriteMode   = kXR_wmode,      //!< the file will be accessed for
                                    //!< modification
      Cancel      = kXR_cancel,     //!< cancel staging request
      Evict       = kXR_evict << 8  //!< evict the file from disk cache
                                    //!< we have to shift kXR_evict as its value
                                    //!< is the same as cancel's because this
                                    //!< flag goes to options extension
    };
  };
  XRDOUC_ENUM_OPERATORS( PrepareFlags::Flags )

  //----------------------------------------------------------------------------
  //! Forward declaration of the implementation class holding the data members
  //----------------------------------------------------------------------------
  struct FileSystemImpl;

  //----------------------------------------------------------------------------
  //! Send file/filesystem queries to an XRootD cluster
  //----------------------------------------------------------------------------
  class FileSystem
  {
    friend class AssignLBHandler;
    friend class ForkHandler;

    public:
      typedef std::vector<LocationInfo> LocationList; //!< Location list

      //------------------------------------------------------------------------
      //! Constructor
      //!
      //! @param url URL of the entry-point server to be contacted
      //! @param enablePlugIns enable the plug-in mechanism for this object
      //------------------------------------------------------------------------
      FileSystem( const URL &url, bool enablePlugIns = true );

      //------------------------------------------------------------------------
      //! Destructor
      //------------------------------------------------------------------------
      ~FileSystem();

      //------------------------------------------------------------------------
      //! Locate a file - async
      //!
      //! @param path    path to the file to be located
      //! @param flags   some of the OpenFlags::Flags
      //! @param handler handler to be notified when the response arrives,
      //!                the response parameter will hold a Buffer object
      //!                if the procedure is successful
      //! @param timeout timeout value, if 0 the environment default will
      //!                be used
      //! @return        status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Locate( const std::string &path,
                           OpenFlags::Flags   flags,
                           ResponseHandler   *handler,
                           time_t             timeout = 0 )
                           XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Locate a file - sync
      //!
      //! @param path     path to the file to be located
      //! @param flags    some of the OpenFlags::Flags
      //! @param response the response (to be deleted by the user)
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Locate( const std::string  &path,
                           OpenFlags::Flags    flags,
                           LocationInfo      *&response,
                           time_t              timeout  = 0 )
                           XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Locate a file, recursively locate all disk servers - async
      //!
      //! @param path    path to the file to be located
      //! @param flags   some of the OpenFlags::Flags
      //! @param handler handler to be notified when the response arrives,
      //!                the response parameter will hold a Buffer object
      //!                if the procedure is successful
      //! @param timeout timeout value, if 0 the environment default will
      //!                be used
      //! @return        status of the operation
      //------------------------------------------------------------------------
      XRootDStatus DeepLocate( const std::string &path,
                               OpenFlags::Flags   flags,
                               ResponseHandler   *handler,
                               time_t             timeout = 0 )
                               XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Locate a file, recursively locate all disk servers - sync
      //!
      //! @param path     path to the file to be located
      //! @param flags    some of the OpenFlags::Flags
      //! @param response the response (to be deleted by the user)
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus DeepLocate( const std::string  &path,
                               OpenFlags::Flags   flags,
                               LocationInfo      *&response,
                               time_t              timeout  = 0 )
                               XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Move a directory or a file - async
      //!
      //! @param source  the file or directory to be moved
      //! @param dest    the new name
      //! @param handler handler to be notified when the response arrives,
      //! @param timeout timeout value, if 0 the environment default will
      //!                be used
      //! @return        status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Mv( const std::string &source,
                       const std::string &dest,
                       ResponseHandler   *handler,
                       time_t             timeout = 0 )
                       XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Move a directory or a file - sync
      //!
      //! @param source  the file or directory to be moved
      //! @param dest    the new name
      //! @param timeout timeout value, if 0 the environment default will
      //!                be used
      //! @return        status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Mv( const std::string &source,
                       const std::string &dest,
                       time_t             timeout = 0 )
                       XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Obtain server information - async
      //!
      //! @param queryCode the query code as specified in the QueryCode struct
      //! @param arg       query argument
      //! @param handler   handler to be notified when the response arrives,
      //!                  the response parameter will hold a Buffer object
      //!                  if the procedure is successful
      //! @param timeout   timeout value, if 0 the environment default will
      //!                  be used
      //! @return          status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Query( QueryCode::Code  queryCode,
                          const Buffer    &arg,
                          ResponseHandler *handler,
                          time_t           timeout = 0 )
                          XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Obtain server information - sync
      //!
      //! @param queryCode the query code as specified in the QueryCode struct
      //! @param arg       query argument
      //! @param response  the response (to be deleted by the user)
      //! @param timeout   timeout value, if 0 the environment default will
      //!                  be used
      //! @return          status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Query( QueryCode::Code   queryCode,
                          const Buffer     &arg,
                          Buffer          *&response,
                          time_t            timeout = 0 )
                          XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Truncate a file - async
      //!
      //! @param path     path to the file to be truncated
      //! @param size     file size
      //! @param handler  handler to be notified when the response arrives
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Truncate( const std::string &path,
                             uint64_t           size,
                             ResponseHandler   *handler,
                             time_t             timeout = 0 )
                             XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Truncate a file - sync
      //!
      //! @param path     path to the file to be truncated
      //! @param size     file size
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Truncate( const std::string &path,
                             uint64_t           size,
                             time_t             timeout = 0 )
                             XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Remove a file - async
      //!
      //! @param path     path to the file to be removed
      //! @param handler  handler to be notified when the response arrives
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Rm( const std::string &path,
                       ResponseHandler   *handler,
                       time_t             timeout = 0 )
                       XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Remove a file - sync
      //!
      //! @param path     path to the file to be removed
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Rm( const std::string &path,
                       time_t             timeout = 0 )
                       XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Create a directory - async
      //!
      //! @param path     path to the directory
      //! @param flags    or'd MkDirFlags
      //! @param mode     access mode, or'd Access::Mode
      //! @param handler  handler to be notified when the response arrives
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus MkDir( const std::string &path,
                          MkDirFlags::Flags  flags,
                          Access::Mode       mode,
                          ResponseHandler   *handler,
                          time_t             timeout = 0 )
                          XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Create a directory - sync
      //!
      //! @param path     path to the directory
      //! @param flags    or'd MkDirFlags
      //! @param mode     access mode, or'd Access::Mode
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus MkDir( const std::string &path,
                          MkDirFlags::Flags  flags,
                          Access::Mode       mode,
                          time_t             timeout = 0 )
                          XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Remove a directory - async
      //!
      //! @param path     path to the directory to be removed
      //! @param handler  handler to be notified when the response arrives
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus RmDir( const std::string &path,
                          ResponseHandler   *handler,
                          time_t             timeout = 0 )
                          XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Remove a directory - sync
      //!
      //! @param path     path to the directory to be removed
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus RmDir( const std::string &path,
                          time_t             timeout = 0 )
                          XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Change access mode on a directory or a file - async
      //!
      //! @param path     file/directory path
      //! @param mode     access mode, or'd Access::Mode
      //! @param handler  handler to be notified when the response arrives
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus ChMod( const std::string &path,
                          Access::Mode       mode,
                          ResponseHandler   *handler,
                          time_t             timeout = 0 )
                          XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Change access mode on a directory or a file - sync
      //!
      //! @param path     file/directory path
      //! @param mode     access mode, or'd Access::Mode
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus ChMod( const std::string &path,
                          Access::Mode       mode,
                          time_t             timeout = 0 )
                          XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Check if the server is alive - async
      //!
      //! @param handler  handler to be notified when the response arrives
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Ping( ResponseHandler *handler,
                         time_t           timeout = 0 )
                         XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Check if the server is alive - sync
      //!
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Ping( time_t timeout = 0 ) XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Obtain status information for a path - async
      //!
      //! @param path    file/directory path
      //! @param handler handler to be notified when the response arrives,
      //!                the response parameter will hold a StatInfo object
      //!                if the procedure is successful
      //! @param timeout timeout value, if 0 the environment default will
      //!                be used
      //! @return        status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Stat( const std::string &path,
                         ResponseHandler   *handler,
                         time_t             timeout = 0 )
                         XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Obtain status information for a path - sync
      //!
      //! @param path     file/directory path
      //! @param response the response (to be deleted by the user only if the
      //!                 procedure is successful)
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Stat( const std::string  &path,
                         StatInfo          *&response,
                         time_t              timeout = 0 )
                         XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Obtain status information for a Virtual File System - async
      //!
      //! @param path    file/directory path
      //! @param handler handler to be notified when the response arrives,
      //!                the response parameter will hold a StatInfoVFS object
      //!                if the procedure is successful
      //! @param timeout timeout value, if 0 the environment default will
      //!                be used
      //! @return        status of the operation
      //------------------------------------------------------------------------
      XRootDStatus StatVFS( const std::string &path,
                            ResponseHandler   *handler,
                            time_t             timeout = 0 )
                            XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Obtain status information for a Virtual File System - sync
      //!
      //! @param path     file/directory path
      //! @param response the response (to be deleted by the user)
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus StatVFS( const std::string  &path,
                            StatInfoVFS       *&response,
                            time_t              timeout = 0 )
                            XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Obtain server protocol information - async
      //!
      //! @param handler handler to be notified when the response arrives,
      //!                the response parameter will hold a ProtocolInfo object
      //!                if the procedure is successful
      //! @param timeout timeout value, if 0 the environment default will
      //!                be used
      //! @return        status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Protocol( ResponseHandler *handler,
                             time_t           timeout = 0 )
                             XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Obtain server protocol information - sync
      //!
      //! @param response the response (to be deleted by the user)
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Protocol( ProtocolInfo *&response,
                             time_t         timeout = 0 )
                             XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! List entries of a directory - async
      //!
      //! @param path    directory path
      //! @param flags   currently unused
      //! @param handler handler to be notified when the response arrives,
      //!                the response parameter will hold a DirectoryList
      //!                object if the procedure is successful
      //! @param timeout timeout value, if 0 the environment default will
      //!                be used
      //! @return        status of the operation
      //------------------------------------------------------------------------
      XRootDStatus DirList( const std::string   &path,
                            DirListFlags::Flags  flags,
                            ResponseHandler     *handler,
                            time_t               timeout = 0 )
                            XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! List entries of a directory - sync
      //!
      //! @param path     directory path
      //! @param flags    DirListFlags
      //! @param response the response (to be deleted by the user)
      //! @param timeout  timeout value, if 0 the environment default will
      //!                 be used
      //! @return         status of the operation
      //------------------------------------------------------------------------
      XRootDStatus DirList( const std::string    &path,
                            DirListFlags::Flags   flags,
                            DirectoryList       *&response,
                            time_t                timeout = 0 )
                            XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Send cache into the server - async
      //!
      //! @param info      the info string to be sent
      //! @param handler   handler to be notified when the response arrives,
      //!                  the response parameter will hold a Buffer object
      //!                  if the procedure is successful
      //! @param timeout   timeout value, if 0 the environment default will
      //!                  be used
      //! @return          status of the operation
      //------------------------------------------------------------------------
      XRootDStatus SendCache( const std::string &info,
                              ResponseHandler   *handler,
                              time_t             timeout = 0 )
                              XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Send cache into the server - sync
      //!
      //! @param info      the info string to be sent
      //! @param response  the response (to be deleted by the user)
      //! @param timeout   timeout value, if 0 the environment default will
      //!                  be used
      //! @return          status of the operation
      //------------------------------------------------------------------------
      XRootDStatus SendCache( const std::string  &info,
                              Buffer            *&response,
                              time_t              timeout = 0 )
                              XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Send info to the server (up to 1024 characters)- async
      //!
      //! @param info      the info string to be sent
      //! @param handler   handler to be notified when the response arrives,
      //!                  the response parameter will hold a Buffer object
      //!                  if the procedure is successful
      //! @param timeout   timeout value, if 0 the environment default will
      //!                  be used
      //! @return          status of the operation
      //------------------------------------------------------------------------
      XRootDStatus SendInfo( const std::string &info,
                             ResponseHandler   *handler,
                             time_t             timeout = 0 )
                             XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Send info to the server (up to 1024 characters) - sync
      //!
      //! @param info      the info string to be sent
      //! @param response  the response (to be deleted by the user)
      //! @param timeout   timeout value, if 0 the environment default will
      //!                  be used
      //! @return          status of the operation
      //------------------------------------------------------------------------
      XRootDStatus SendInfo( const std::string  &info,
                             Buffer            *&response,
                             time_t              timeout = 0 )
                             XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Prepare one or more files for access - async
      //!
      //! @param fileList  list of files to be prepared
      //! @param flags     PrepareFlags::Flags
      //! @param priority  priority of the request 0 (lowest) - 3 (highest)
      //! @param handler   handler to be notified when the response arrives,
      //!                  the response parameter will hold a Buffer object
      //!                  if the procedure is successful
      //! @param timeout   timeout value, if 0 the environment default will
      //!                  be used
      //! @return          status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Prepare( const std::vector<std::string> &fileList,
                            PrepareFlags::Flags             flags,
                            uint8_t                         priority,
                            ResponseHandler                *handler,
                            time_t                          timeout = 0 )
                            XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Prepare one or more files for access - sync
      //!
      //! @param fileList  list of files to be prepared
      //! @param flags     PrepareFlags::Flags
      //! @param priority  priority of the request 0 (lowest) - 3 (highest)
      //! @param response  the response (to be deleted by the user)
      //! @param timeout   timeout value, if 0 the environment default will
      //!                  be used
      //! @return          status of the operation
      //------------------------------------------------------------------------
      XRootDStatus Prepare( const std::vector<std::string>  &fileList,
                            PrepareFlags::Flags              flags,
                            uint8_t                          priority,
                            Buffer                         *&response,
                            time_t                           timeout = 0 )
                            XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Set extended attributes - async
      //!
      //! @param attrs   : list of extended attributes to set
      //! @param handler : handler to be notified when the response arrives,
      //!                  the response parameter will hold a std::vector of
      //!                  XAttrStatus objects
      //! @param timeout : timeout value, if 0 the environment default will
      //!                  be used
      //!
      //! @return        : status of the operation
      //------------------------------------------------------------------------
      XRootDStatus SetXAttr( const std::string           &path,
                             const std::vector<xattr_t>  &attrs,
                             ResponseHandler             *handler,
                             time_t                       timeout = 0 );

      //------------------------------------------------------------------------
      //! Set extended attributes - sync
      //!
      //! @param attrs   : list of extended attributes to set
      //! @param result  : result of the operation
      //! @param timeout : timeout value, if 0 the environment default will
      //!                  be used
      //!
      //! @return        : status of the operation
      //------------------------------------------------------------------------
      XRootDStatus SetXAttr( const std::string           &path,
                             const std::vector<xattr_t>  &attrs,
                             std::vector<XAttrStatus>    &result,
                             time_t                       timeout = 0 );

      //------------------------------------------------------------------------
      //! Get extended attributes - async
      //!
      //! @param attrs   : list of extended attributes to get
      //! @param handler : handler to be notified when the response arrives,
      //!                  the response parameter will hold a std::vector of
      //!                  XAttr objects
      //! @param timeout : timeout value, if 0 the environment default will
      //!                  be used
      //!
      //! @return        : status of the operation
      //------------------------------------------------------------------------
      XRootDStatus GetXAttr( const std::string               &path,
                             const std::vector<std::string>  &attrs,
                             ResponseHandler                 *handler,
                             time_t                           timeout = 0 );

      //------------------------------------------------------------------------
      //! Get extended attributes - sync
      //!
      //! @param attrs   : list of extended attributes to get
      //! @param result  : result of the operation
      //! @param timeout : timeout value, if 0 the environment default will
      //!                  be used
      //!
      //! @return        : status of the operation
      //------------------------------------------------------------------------
      XRootDStatus GetXAttr( const std::string               &path,
                             const std::vector<std::string>  &attrs,
                             std::vector<XAttr>              &result,
                             time_t                           timeout = 0 );

      //------------------------------------------------------------------------
      //! Delete extended attributes - async
      //!
      //! @param attrs   : list of extended attributes to set
      //! @param handler : handler to be notified when the response arrives,
      //!                  the response parameter will hold a std::vector of
      //!                  XAttrStatus objects
      //! @param timeout : timeout value, if 0 the environment default will
      //!                  be used
      //!
      //! @return        : status of the operation
      //------------------------------------------------------------------------
      XRootDStatus DelXAttr( const std::string               &path,
                             const std::vector<std::string>  &attrs,
                             ResponseHandler                 *handler,
                             time_t                           timeout = 0 );

      //------------------------------------------------------------------------
      //! Delete extended attributes - sync
      //!
      //! @param attrs   : list of extended attributes to set
      //! @param result  : result of the operation
      //! @param timeout : timeout value, if 0 the environment default will
      //!                  be used
      //!
      //! @return        : status of the operation
      //------------------------------------------------------------------------
      XRootDStatus DelXAttr( const std::string               &path,
                             const std::vector<std::string>  &attrs,
                             std::vector<XAttrStatus>        &result,
                             time_t                           timeout = 0 );

      //------------------------------------------------------------------------
      //! List extended attributes - async
      //!
      //! @param handler : handler to be notified when the response arrives,
      //!                  the response parameter will hold a std::vector of
      //!                  XAttr objects
      //! @param timeout : timeout value, if 0 the environment default will
      //!                  be used
      //!
      //! @return        : status of the operation
      //------------------------------------------------------------------------
      XRootDStatus ListXAttr( const std::string         &path,
                              ResponseHandler           *handler,
                              time_t                     timeout = 0 );

      //------------------------------------------------------------------------
      //! List extended attributes - sync
      //!
      //! @param result  : result of the operation
      //! @param timeout : timeout value, if 0 the environment default will
      //!                  be used
      //!
      //! @return        : status of the operation
      //------------------------------------------------------------------------
      XRootDStatus ListXAttr( const std::string    &path,
                              std::vector<XAttr>   &result,
                              time_t                timeout = 0 );

      //------------------------------------------------------------------------
      //! Set filesystem property
      //!
      //! Filesystem properties:
      //! FollowRedirects  [true/false] - enable/disable following redirections
      //------------------------------------------------------------------------
      bool SetProperty( const std::string &name, const std::string &value );

      //------------------------------------------------------------------------
      //! Get filesystem property
      //!
      //! @see FileSystem::SetProperty for property list
      //------------------------------------------------------------------------
      bool GetProperty( const std::string &name, std::string &value ) const;

    private:
      FileSystem(const FileSystem &other);
      FileSystem &operator = (const FileSystem &other);

      //------------------------------------------------------------------------
      // Lock the internal lock
      //------------------------------------------------------------------------
      void Lock();

      //------------------------------------------------------------------------
      // Unlock the internal lock
      //------------------------------------------------------------------------
      void UnLock();

      //------------------------------------------------------------------------
      //! Generic implementation of SendCache and SendInfo
      //!
      //! @param info    : the info string to be sent
      //! @param handler : handler to be notified when the response arrives.
      //! @param timeout : timeout value or 0 for default.
      //! @return          status of the operation
      //------------------------------------------------------------------------
      XRootDStatus SendSet( const char        *prefix,
                            const std::string &info,
                            ResponseHandler   *handler,
                            time_t             timeout = 0 )
                            XRD_WARN_UNUSED_RESULT;

      //------------------------------------------------------------------------
      //! Generic implementation of xattr operation
      //!
      //! @param subcode : xattr operation code
      //! @param path    : path to the file
      //! @param attrs   : operation argument
      //! @param handler : operation handler
      //! @param timeout : operation timeout
      //------------------------------------------------------------------------
      template<typename T>
      Status XAttrOperationImpl( kXR_char              subcode,
                                 kXR_char              options,
                                 const std::string    &path,
                                 const std::vector<T> &attrs,
                                 ResponseHandler      *handler,
                                 time_t                timeout = 0 );

      FileSystemImpl   *pImpl;   //< pointer to implementation (TODO: once we can break ABI we can use a shared pointer here, and then we can drop the FileSystemData in source file)
      FileSystemPlugIn *pPlugIn; //< file system plug-in
  };
}

#endif // __XRD_CL_FILE_SYSTEM_HH__
