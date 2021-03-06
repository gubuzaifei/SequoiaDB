/*******************************************************************************

   Copyright (C) 2011-2014 SequoiaDB Ltd.

   This program is free software: you can redistribute it and/or modify
   it under the term of the GNU Affero General Public License, version 3,
   as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warrenty of
   MARCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program. If not, see <http://www.gnu.org/license/>.

   Source File Name = rtnCB.cpp

   Descriptive Name = Runtime Control Block

   When/how to use: this program may be used on binary and text-formatted
   versions of runtime component. This file contains code logic for
   control block.

   Dependencies: N/A

   Restrictions: N/A

   Change Activity:
   defect Date        Who Description
   ====== =========== === ==============================================

   Last Changed =

*******************************************************************************/

#include "rtnCB.hpp"
#include "pdTrace.hpp"
#include "rtnTrace.hpp"
#include "rtnContextSort.hpp"
#include "rtnContextLob.hpp"
#include "rtnContextShdOfLob.hpp"
#include "rtnContextListLob.hpp"
#include "catContextData.hpp"
#include "catContextNode.hpp"
#include "dmsCB.hpp"
#include "rtnIxmKeySorter.hpp"
#include "../omsvc/omContextTransfer.hpp"

using namespace std;
namespace engine
{

   _SDB_RTNCB::_SDB_RTNCB()
   {
      _contextHWM = 0 ;
   }

   _SDB_RTNCB::~_SDB_RTNCB()
   {
      std::map<SINT64, rtnContext *>::const_iterator it ;
      for ( it = _contextList.begin(); it != _contextList.end(); it++ )
      {
         SDB_OSS_DEL ((*it).second) ;
      }
      _contextList.clear() ;
   }

   INT32 _SDB_RTNCB::init ()
   {
      INT32 rc = SDB_OK ;

      rtnIxmKeySorterCreator* creator = SDB_OSS_NEW _rtnIxmKeySorterCreator() ;
      if ( NULL == creator )
      {
         PD_LOG ( PDERROR, "failed to create _rtnIxmKeySorterCreator" ) ;
         rc = SDB_OOM ;
         goto error ;
      }

      sdbGetDMSCB()->setIxmKeySorterCreator( creator ) ;

   done:
      return rc ;
   error:
      goto done ;
   }

   INT32 _SDB_RTNCB::active ()
   {
      return SDB_OK ;
   }

   INT32 _SDB_RTNCB::deactive ()
   {
      return SDB_OK ;
   }

   INT32 _SDB_RTNCB::fini ()
   {
      dmsIxmKeySorterCreator* creator = sdbGetDMSCB()->getIxmKeySorterCreator() ;
      if ( NULL != creator )
      {
         SDB_OSS_DEL( creator ) ;
         sdbGetDMSCB()->setIxmKeySorterCreator( NULL ) ;
      }

      return SDB_OK ;
   }

   rtnContext* _SDB_RTNCB::contextFind ( SINT64 contextID, _pmdEDUCB *cb )
   {
      rtnContext *pContext = NULL ;

      _mutex.get_shared() ;
      std::map<SINT64, rtnContext*>::iterator it ;
      if ( _contextList.end() != ( it = _contextList.find( contextID ) ) )
      {
         pContext = (*it).second ;
      }
      _mutex.release_shared() ;

      if ( pContext && cb && !cb->contextFind( contextID ) )
      {
         PD_LOG ( PDWARNING, "Context %lld does not owned by "
                  "current session", contextID ) ;
         pContext = NULL ;
      }
      return pContext ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB__SDB_RTNCB_CONTEXTDEL, "_SDB_RTNCB::contextDelete" )
   void _SDB_RTNCB::contextDelete ( SINT64 contextID, pmdEDUCB *cb )
   {
      PD_TRACE_ENTRY ( SDB__SDB_RTNCB_CONTEXTDEL ) ;

      rtnContext *pContext = NULL ;
      std::map<SINT64, rtnContext*>::iterator it ;

      if ( cb )
      {
         cb->contextDelete( contextID ) ;
      }

      {
         RTNCB_XLOCK
         it = _contextList.find( contextID ) ;
         if ( _contextList.end() != it )
         {
            pContext = it->second;
            _contextList.erase( it ) ;
         }
      }

      if ( pContext )
      {
         INT32 reference = pContext->getReference() ;
         pContext->waitForPrefetch() ;

         if ( pContext->isWrite() && pContext->getDPSCB() &&
              pContext->getW() > 1 )
         {
            pContext->getDPSCB()->completeOpr( cb, pContext->getW() ) ;
         }

         SDB_OSS_DEL pContext ;
         PD_LOG( PDDEBUG, "delete context(contextID=%lld, reference: %d)",
                 contextID, reference ) ;
      }

      PD_TRACE_EXIT ( SDB__SDB_RTNCB_CONTEXTDEL ) ;
      return ;
   }

   SINT32 _SDB_RTNCB::contextNew ( RTN_CONTEXT_TYPE type,
                                   rtnContext **context,
                                   SINT64 &contextID,
                                   _pmdEDUCB * pEDUCB )
   {
      SDB_ASSERT ( context, "context pointer can't be NULL" ) ;
      {
         RTNCB_XLOCK
         if ( _contextHWM+1 < 0 )
         {
            return SDB_SYS ;
         }

         switch ( type )
         {
            case RTN_CONTEXT_DATA :
               (*context) = SDB_OSS_NEW rtnContextData ( _contextHWM,
                                                         pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_DUMP :
               (*context) = SDB_OSS_NEW rtnContextDump ( _contextHWM,
                                                         pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_COORD :
               (*context) = SDB_OSS_NEW rtnContextCoord ( _contextHWM,
                                                          pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_QGM :
               (*context) = SDB_OSS_NEW rtnContextQGM ( _contextHWM,
                                                        pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_TEMP :
               (*context) = SDB_OSS_NEW rtnContextTemp ( _contextHWM,
                                                         pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_SP :
               (*context) = SDB_OSS_NEW rtnContextSP ( _contextHWM,
                                                       pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_PARADATA :
               (*context) = SDB_OSS_NEW rtnContextParaData( _contextHWM,
                                                            pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_MAINCL :
               (*context) = SDB_OSS_NEW rtnContextMainCL( _contextHWM,
                                                         pEDUCB->getID() );
               break;
            case RTN_CONTEXT_SORT :
               (*context) = SDB_OSS_NEW rtnContextSort( _contextHWM,
                                                        pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_QGMSORT :
               (*context) = SDB_OSS_NEW rtnContextQgmSort( _contextHWM,
                                                            pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_DELCS :
               (*context) = SDB_OSS_NEW rtnContextDelCS( _contextHWM,
                                                            pEDUCB->getID() ) ;
               break;
            case RTN_CONTEXT_DELCL :
               (*context) = SDB_OSS_NEW rtnContextDelCL( _contextHWM,
                                                            pEDUCB->getID() ) ;
               break;
            case RTN_CONTEXT_DELMAINCL :
               (*context) = SDB_OSS_NEW rtnContextDelMainCL( _contextHWM,
                                                            pEDUCB->getID() ) ;
               break;
            case RTN_CONTEXT_EXPLAIN :
                (*context) = SDB_OSS_NEW rtnContextExplain( _contextHWM,
                                                            pEDUCB->getID() ) ;
                break ;
            case RTN_CONTEXT_LOB :
                 (*context) = SDB_OSS_NEW rtnContextLob( _contextHWM,
                                                         pEDUCB->getID() ) ;
                break ;
            case RTN_CONTEXT_SHARD_OF_LOB :
                 (*context) = SDB_OSS_NEW rtnContextShdOfLob( _contextHWM,
                                                              pEDUCB->getID() ) ;
                break ;
            case RTN_CONTEXT_LIST_LOB:
                 (*context) = SDB_OSS_NEW rtnContextListLob( _contextHWM,
                                                             pEDUCB->getID() ) ;
                break ;
            case RTN_CONTEXT_OM_TRANSFER:
                 (*context) = SDB_OSS_NEW omContextTransfer( _contextHWM,
                                                             pEDUCB->getID() ) ;
                 break;
            case RTN_CONTEXT_LOB_FETCHER:
                 (*context) = SDB_OSS_NEW rtnContextLobFetcher( _contextHWM,
                                                                pEDUCB->getID() ) ;
                 break ;

            case RTN_CONTEXT_CAT_DROP_CS :
               (*context) = SDB_OSS_NEW catCtxDropCS( _contextHWM,
                                                      pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_CAT_CREATE_CL :
               (*context) = SDB_OSS_NEW catCtxCreateCL( _contextHWM,
                                                        pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_CAT_DROP_CL :
               (*context) = SDB_OSS_NEW catCtxDropCL( _contextHWM,
                                                      pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_CAT_ALTER_CL :
               (*context) = SDB_OSS_NEW catCtxAlterCL( _contextHWM,
                                                       pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_CAT_LINK_CL :
               (*context) = SDB_OSS_NEW catCtxLinkCL( _contextHWM,
                                                      pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_CAT_UNLINK_CL :
               (*context) = SDB_OSS_NEW catCtxUnlinkCL( _contextHWM,
                                                        pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_CAT_CREATE_IDX :
               (*context) = SDB_OSS_NEW catCtxCreateIdx( _contextHWM,
                                                         pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_CAT_DROP_IDX :
               (*context) = SDB_OSS_NEW catCtxDropIdx( _contextHWM,
                                                       pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_CAT_ACTIVE_GROUP :
               (*context) = SDB_OSS_NEW catCtxActiveGrp( _contextHWM,
                                                         pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_CAT_SHUTDOWN_GROUP :
               (*context) = SDB_OSS_NEW catCtxShutdownGrp( _contextHWM,
                                                           pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_CAT_REMOVE_GROUP :
               (*context) = SDB_OSS_NEW catCtxRemoveGrp( _contextHWM,
                                                         pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_CAT_CREATE_NODE :
               (*context) = SDB_OSS_NEW catCtxCreateNode( _contextHWM,
                                                          pEDUCB->getID() ) ;
               break ;
            case RTN_CONTEXT_CAT_REMOVE_NODE :
               (*context) = SDB_OSS_NEW catCtxRemoveNode( _contextHWM,
                                                          pEDUCB->getID() ) ;
               break ;

            default :
               PD_LOG( PDERROR, "Unknown context type: %d", type ) ;
               return SDB_SYS ;
         }

         if ( !(*context) )
         {
            return SDB_OOM ;
         }

         _contextList[_contextHWM] = *context ;
         pEDUCB->contextInsert( _contextHWM ) ;
         contextID = _contextHWM ;
         ++_contextHWM ;
      }
      PD_LOG ( PDDEBUG, "Create new context(contextID=%lld, type: %d[%s])",
               contextID, type, getContextTypeDesp(type) ) ;
      return SDB_OK ;
   }

   /*
      get global rtn cb
   */
   SDB_RTNCB* sdbGetRTNCB ()
   {
      static SDB_RTNCB s_rtnCB ;
      return &s_rtnCB ;
   }

}

