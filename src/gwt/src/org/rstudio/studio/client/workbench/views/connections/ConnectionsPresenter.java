/*
 * ConnectionsPresenter.java
 *
 * Copyright (C) 2009-12 by RStudio, Inc.
 *
 * This program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */
package org.rstudio.studio.client.workbench.views.connections;

import java.util.ArrayList;
import java.util.List;

import com.google.gwt.core.client.JsArray;
import com.google.gwt.event.dom.client.ClickEvent;
import com.google.gwt.event.dom.client.ClickHandler;
import com.google.gwt.event.dom.client.HasClickHandlers;
import com.google.gwt.event.logical.shared.ValueChangeEvent;
import com.google.gwt.event.logical.shared.ValueChangeHandler;
import com.google.gwt.event.shared.HandlerRegistration;
import com.google.gwt.view.client.SelectionChangeEvent;
import com.google.inject.Inject;

import org.rstudio.core.client.ListUtil;
import org.rstudio.core.client.ListUtil.FilterPredicate;
import org.rstudio.core.client.command.CommandBinder;
import org.rstudio.core.client.command.Handler;
import org.rstudio.core.client.events.EnsureHeightEvent;
import org.rstudio.core.client.js.JsObject;
import org.rstudio.core.client.widget.MessageDialog;
import org.rstudio.core.client.widget.Operation;
import org.rstudio.studio.client.common.GlobalDisplay;
import org.rstudio.studio.client.server.VoidServerRequestCallback;
import org.rstudio.studio.client.workbench.WorkbenchListManager;
import org.rstudio.studio.client.workbench.WorkbenchView;
import org.rstudio.studio.client.workbench.commands.Commands;
import org.rstudio.studio.client.workbench.model.ClientState;
import org.rstudio.studio.client.workbench.model.Session;
import org.rstudio.studio.client.workbench.model.SessionInfo;
import org.rstudio.studio.client.workbench.model.helper.JSObjectStateValue;
import org.rstudio.studio.client.workbench.views.BasePresenter;
import org.rstudio.studio.client.workbench.views.connections.events.ActiveConnectionsChangedEvent;
import org.rstudio.studio.client.workbench.views.connections.events.ConnectionListChangedEvent;
import org.rstudio.studio.client.workbench.views.connections.events.ConnectionUpdatedEvent;
import org.rstudio.studio.client.workbench.views.connections.events.ExploreConnectionEvent;
import org.rstudio.studio.client.workbench.views.connections.model.Connection;
import org.rstudio.studio.client.workbench.views.connections.model.ConnectionId;
import org.rstudio.studio.client.workbench.views.connections.model.ConnectionsServerOperations;

public class ConnectionsPresenter extends BasePresenter 
{
   public interface Display extends WorkbenchView
   {
      void setConnections(List<Connection> connections);
      void setActiveConnections(List<ConnectionId> connections);
      
      Connection getSelectedConnection();
          
      HandlerRegistration addSelectedConnectionChangeHandler(
                                 SelectionChangeEvent.Handler handler);
      
      HandlerRegistration addSearchFilterChangeHandler(
                                       ValueChangeHandler<String> handler);
      
      HandlerRegistration addExploreConnectionHandler(
                                       ExploreConnectionEvent.Handler handler);
      
      void showConnectionExplorer(Connection connection);
      
      
      HasClickHandlers backToConnectionsButton();
      
      void showConnectionsList();
   }
   
   public interface Binder extends CommandBinder<Commands, ConnectionsPresenter> {}
   
   @Inject
   public ConnectionsPresenter(Display display, 
                               ConnectionsServerOperations server,
                               GlobalDisplay globalDisplay,
                               Binder binder,
                               final Commands commands,
                               WorkbenchListManager listManager,
                               Session session)
   {
      super(display);
      binder.bind(commands, this);
      display_ = display;
      server_ = server;
      globalDisplay_ = globalDisplay;
         
       
      // track selected connection
      display_.addSelectedConnectionChangeHandler(
                                       new SelectionChangeEvent.Handler() {
         @Override
         public void onSelectionChange(SelectionChangeEvent event)
         {
            
         }
      });
      
      // search filter
      display_.addSearchFilterChangeHandler(new ValueChangeHandler<String>() {

         @Override
         public void onValueChange(ValueChangeEvent<String> event)
         {
            String query = event.getValue();
            final String[] splat = query.toLowerCase().split("\\s+");
            List<Connection> connections = ListUtil.filter(allConnections_, 
                                         new FilterPredicate<Connection>()
            {
               @Override
               public boolean test(Connection connection)
               {
                  for (String el : splat)
                  {
                     boolean match =
                         connection.getHost().toLowerCase().contains(el);
                     if (!match)
                        return false;
                  }
                  return true;
               }
            });
            display_.setConnections(connections);
         }
      });
      
      display_.addExploreConnectionHandler(new ExploreConnectionEvent.Handler()
      {   
         @Override
         public void onExploreConnection(ExploreConnectionEvent event)
         {
            exploreConnection(event.getConnection());
            display_.ensureHeight(EnsureHeightEvent.MAXIMIZED);
         }
      });
      
      display_.backToConnectionsButton().addClickHandler(new ClickHandler() {

         @Override
         public void onClick(ClickEvent event)
         {
            exploredConnection_ = null;
            display_.showConnectionsList();
            display_.ensureHeight(EnsureHeightEvent.NORMAL);
         }
         
      });
      
      // set connections      
      SessionInfo sessionInfo = session.getSessionInfo();
      updateConnections(sessionInfo.getConnectionList());  
      updateActiveConnections(sessionInfo.getActiveConnections());
           
      // make the explored connection persistent
      new JSObjectStateValue(MODULE_CONNECTIONS, 
                             KEY_EXPLORED_CONNECTION, 
                             ClientState.PERSISTENT, 
                             session.getSessionInfo().getClientState(), 
                             false)
      {
         @Override
         protected void onInit(JsObject value)
         {
            // get the value
            if (value != null)
               exploredConnection_ = value.cast();
            else
               exploredConnection_ = null;
                 
            lastExploredConnection_ = exploredConnection_;
            
            // if there is an an explored connection then explore it
            if (exploredConnection_ != null)
               exploreConnection(exploredConnection_);
         }

         @Override
         protected JsObject getValue()
         {
            if (exploredConnection_ != null)
               return exploredConnection_.cast();
            else
               return null;
         }

         @Override
         protected boolean hasChanged()
         {
            if (lastExploredConnection_ != exploredConnection_)
            {
               lastExploredConnection_ = exploredConnection_;
               return true;
            }
            else
            {
               return false;
            }
         }
      };
   }
   
   public void onConnectionUpdated(ConnectionUpdatedEvent event)
   {     
   }
   
   public void onConnectionListChanged(ConnectionListChangedEvent event)
   {
      updateConnections(event.getConnectionList());
   }
   
   public void onActiveConnectionsChanged(ActiveConnectionsChangedEvent event)
   {
      updateActiveConnections(event.getActiveConnections());
   }
   
   public void onNewConnection()
   {
      globalDisplay_.showErrorMessage("Error", "Not Yet Implemented");
   }
   
   @Handler
   public void onRemoveConnection()
   {
      final Connection connection = display_.getSelectedConnection();
      if (connection != null)
      {
         globalDisplay_.showYesNoMessage(
            MessageDialog.QUESTION,
            "Remove Connection",
            "Are you sure you want to remove the selected connection from " +
            "the list?", 
            new Operation() {
   
               @Override
               public void execute()
               {
                  server_.removeConnection(
                    connection.getId(), new VoidServerRequestCallback());   
               }
            },
            true);
      }
      else
      {
         globalDisplay_.showErrorMessage(
           "Remove Connection", "No connection currently selected.");
      }
   }
  
   private void updateConnections(JsArray<Connection> connections)
   {
      allConnections_.clear();
      for (int i = 0; i<connections.length(); i++)
         allConnections_.add(connections.get(i));
      display_.setConnections(allConnections_);
   }
   
   private void updateActiveConnections(JsArray<ConnectionId> connections)
   {
      activeConnections_.clear();
      for (int i = 0; i<connections.length(); i++)
         activeConnections_.add(connections.get(i));
      display_.setActiveConnections(activeConnections_);
   }
   
   private void exploreConnection(Connection connection)
   {
      exploredConnection_ = connection;
      display_.showConnectionExplorer(connection);
   }
   
   private final GlobalDisplay globalDisplay_;
   
   private final Display display_ ;
   private final ConnectionsServerOperations server_ ;
   
   // client state
   private static final String MODULE_CONNECTIONS = "connections-pane";
   private static final String KEY_EXPLORED_CONNECTION = "exploredConnection";
   private Connection exploredConnection_;
   private Connection lastExploredConnection_;
   
   private ArrayList<Connection> allConnections_ = new ArrayList<Connection>();
   private ArrayList<ConnectionId> activeConnections_ = new ArrayList<ConnectionId>();
}