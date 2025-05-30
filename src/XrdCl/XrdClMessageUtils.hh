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

#ifndef __XRD_CL_MESSAGE_UTILS_HH__
#define __XRD_CL_MESSAGE_UTILS_HH__

#include "XrdCl/XrdClXRootDResponses.hh"
#include "XrdCl/XrdClURL.hh"
#include "XrdCl/XrdClMessage.hh"
#include "XrdSys/XrdSysKernelBuffer.hh"
#include "XrdSys/XrdSysPthread.hh"

namespace XrdCl
{
  class LocalFileHandler;

  struct XAttr;

  //----------------------------------------------------------------------------
  //! Synchronize the response
  //----------------------------------------------------------------------------
  class SyncResponseHandler: public ResponseHandler
  {
    public:
      //------------------------------------------------------------------------
      //! Constructor
      //------------------------------------------------------------------------
      SyncResponseHandler():
        pStatus(0),
        pResponse(0),
        pCondVar(0) {}

      //------------------------------------------------------------------------
      //! Destructor
      //------------------------------------------------------------------------
      virtual ~SyncResponseHandler()
      {
      }


      //------------------------------------------------------------------------
      //! Handle the response
      //------------------------------------------------------------------------
      virtual void HandleResponse( XRootDStatus *status,
                                   AnyObject    *response )
      {
        XrdSysCondVarHelper scopedLock(pCondVar);
        pStatus = status;
        pResponse = response;
        pCondVar.Broadcast();
      }

      //------------------------------------------------------------------------
      //! Get the status
      //------------------------------------------------------------------------
      XRootDStatus *GetStatus()
      {
        return pStatus;
      }

      //------------------------------------------------------------------------
      //! Get the response
      //------------------------------------------------------------------------
      AnyObject *GetResponse()
      {
        return pResponse;
      }

      //------------------------------------------------------------------------
      //! Wait for the arrival of the response
      //------------------------------------------------------------------------
      void WaitForResponse()
      {
        XrdSysCondVarHelper scopedLock(pCondVar);
        while (pStatus == 0) {
          pCondVar.Wait();
        }
      }

    private:
      SyncResponseHandler(const SyncResponseHandler &other);
      SyncResponseHandler &operator = (const SyncResponseHandler &other);

      XRootDStatus    *pStatus;
      AnyObject       *pResponse;
      XrdSysCondVar    pCondVar;
  };


  //----------------------------------------------------------------------------
  // We're not interested in the response just commit suicide
  //----------------------------------------------------------------------------
  class NullResponseHandler: public XrdCl::ResponseHandler
  {
    public:
      //------------------------------------------------------------------------
      // Handle the response
      //------------------------------------------------------------------------
      virtual void HandleResponseWithHosts( XrdCl::XRootDStatus *status,
                                            XrdCl::AnyObject    *response,
                                            XrdCl::HostList     *hostList )
      {
        delete this;
      }
  };

  //----------------------------------------------------------------------------
  // Sending parameters
  //----------------------------------------------------------------------------
  struct MessageSendParams
  {
    MessageSendParams():
      timeout(0), expires(0), followRedirects(true), chunkedResponse(false),
      stateful(true), hostList(0), chunkList(0), redirectLimit(0), kbuff(0){}
    time_t                 timeout;
    time_t                 expires;
    HostInfo               loadBalancer;
    bool                   followRedirects;
    bool                   chunkedResponse;
    bool                   stateful;
    HostList              *hostList;
    ChunkList             *chunkList;
    uint16_t               redirectLimit;
    XrdSys::KernelBuffer  *kbuff;
    std::vector<uint32_t>  crc32cDigests;
  };

  class MessageUtils
  {
    public:
      //------------------------------------------------------------------------
      //! Wait and return the status of the query
      //------------------------------------------------------------------------
      static XRootDStatus WaitForStatus( SyncResponseHandler *handler )
      {
        handler->WaitForResponse();
        XRootDStatus *status = handler->GetStatus();
        XRootDStatus ret( *status );
        delete status;
        return ret;
      }

      //------------------------------------------------------------------------
      //! Wait for the response
      //------------------------------------------------------------------------
      template<class Type>
      static XrdCl::XRootDStatus WaitForResponse(
                            SyncResponseHandler  *handler,
                            Type                *&response )
      {
        handler->WaitForResponse();

        AnyObject    *resp   = handler->GetResponse();
        XRootDStatus *status = handler->GetStatus();
        XRootDStatus ret( *status );
        delete status;

        if( ret.IsOK() )
        {
          if( !resp )
            return XRootDStatus( stError, errInternal );
          resp->Get( response );
          resp->Set( (int *)0 );
          delete resp;

          if( !response )
            return XRootDStatus( stError, errInternal );
        }

        return ret;
      }

      //------------------------------------------------------------------------
      //! Create a message
      //------------------------------------------------------------------------
      template<class Request>
      static void CreateRequest( Message  *&msg,
                                 Request     *&req,
                                 uint32_t  payloadSize = 0 )
      {
          msg = new Message( sizeof(Request) +  payloadSize );
          req = (Request*)msg->GetBuffer();
          msg->Zero();
      }

      //------------------------------------------------------------------------
      //! Send message
      //------------------------------------------------------------------------
      static XRootDStatus SendMessage( const URL         &url,
                                       Message           *msg,
                                       ResponseHandler   *handler,
                                       MessageSendParams &sendParams,
                                       LocalFileHandler  *lFileHandler );

      //------------------------------------------------------------------------
      //! Redirect message
      //------------------------------------------------------------------------
      static Status RedirectMessage( const URL         &url,
                                     Message           *msg,
                                     ResponseHandler   *handler,
                                     MessageSendParams &sendParams,
                                     LocalFileHandler  *lFileHandler );

      //------------------------------------------------------------------------
      //! Process sending params
      //------------------------------------------------------------------------
      static void ProcessSendParams( MessageSendParams &sendParams );

      //------------------------------------------------------------------------
      //! Rewrite CGI and path if necessary
      //!
      //! @param msg     message concerned
      //! @param newCgi  the new cgi
      //! @param replace indicates whether, in case of a conflict, the new CGI
      //!                parameter should replace an existing one or be
      //!                appended to it using a comma
      //! @param newPath will be used as the new destination path if it is
      //!                not empty
      //------------------------------------------------------------------------
      static void RewriteCGIAndPath( Message              *msg,
                                     const URL::ParamsMap &newCgi,
                                     bool                  replace,
                                     const std::string    &newPath );

      //------------------------------------------------------------------------
      //! Merge cgi2 into cgi1
      //!
      //! @param cgi1    cgi to be merged into
      //! @param cgi2    cgi to be merged in
      //! @param replace indicates whether, in case of a conflict, the new CGI
      //!                parameter should replace an existing one or be
      //!                appended to it using a comma
      //------------------------------------------------------------------------
      static void MergeCGI( URL::ParamsMap       &cgi1,
                            const URL::ParamsMap &cgi2,
                            bool                  replace );

      //------------------------------------------------------------------------
      //! Create xattr vector
      //!
      //! @param attrs  :  extended attribute list
      //! @param avec   :  vector containing the name vector
      //!                  and the value vector
      //------------------------------------------------------------------------
      static Status CreateXAttrVec( const std::vector<xattr_t> &attrs,
                                    std::vector<char>          &avec );

      //------------------------------------------------------------------------
      //! Create xattr name vector vector
      //!
      //! @param attrs  :  extended attribute name list
      //! @param nvec   :  vector containing the name vector
      //------------------------------------------------------------------------
      static Status CreateXAttrVec( const std::vector<std::string> &attrs,
                                    std::vector<char>              &nvec );

      //------------------------------------------------------------------------
      //! Create body of xattr request and set the body size
      //!
      //! @param msg  : the request
      //! @param vec  : the argument
      //! @param path : file path
      //------------------------------------------------------------------------
      template<typename T>
      static Status CreateXAttrBody( Message               *msg,
                                     const std::vector<T>  &vec,
                                     const std::string     &path = "" )
      {
        ClientRequestHdr *hdr = reinterpret_cast<ClientRequestHdr*>( msg->GetBuffer() );

        std::vector<char> xattrvec;
        Status st = MessageUtils::CreateXAttrVec( vec, xattrvec );
        if( !st.IsOK() )
          return st;

        // update body size in the header
        hdr->dlen  = path.size() + 1;
        hdr->dlen += xattrvec.size();

        // append the body
        size_t offset = sizeof( ClientRequestHdr );
        msg->Append( path.c_str(), path.size() + 1, offset );
        offset += path.size() + 1;
        msg->Append( xattrvec.data(), xattrvec.size(), offset );

        return Status();
      }
  };
}

#endif // __XRD_CL_MESSAGE_UTILS_HH__
