/*jslint indent: 2, nomen: true, maxlen: 100, vars: true, white: true, plusplus: true, stupid: true */
/*global require, window, exports, Backbone, $, arangoHelper, templateEngine */
(function() {
  "use strict";
  window.CollectionView = Backbone.View.extend({
    el: '#modalPlaceholder',
    initialize: function () {
      var self = this;
      $.ajax("cluster/amICoordinator", {
       async: false  
     }).done(function(d) {
        self.isCoordinator = d;
      });
    },

    template: templateEngine.createTemplate("collectionView.ejs"),

    render: function() {
      var self = this;
      $(this.el).html(this.template.render({
        isCoordinator: self.isCoordinator
      }));
      $('#change-collection').modal('show');
      $('#change-collection').on('hidden', function () {
      });
      $('#change-collection').on('shown', function () {
        if (! self.isCoordinator) {
          $('#change-collection-name').focus();
        }
      });
      this.fillModal();

      $("[data-toggle=tooltip]").tooltip();

      return this;
    },
    events: {
      "click #save-modified-collection"       :    "saveModifiedCollection",
      "hidden #change-collection"             :    "hidden",
      "click #delete-modified-collection"     :    "deleteCollection",
      "click #load-modified-collection"       :    "loadCollection",
      "click #unload-modified-collection"     :    "unloadCollection",
      "click #confirmDeleteCollection"        :    "confirmDeleteCollection",
      "click #abortDeleteCollection"          :    "abortDeleteCollection",
      "keydown #change-collection-name"       :    "listenKey",
      "keydown #change-collection-size"       :    "listenKey"
    },
    listenKey: function(e) {
      if (e.keyCode === 13) {
        this.saveModifiedCollection();
      }
    },
    hidden: function () {
      window.App.navigate("#collections", {trigger: true});
    },

    setColId: function(colId) {
      this.options.colId = colId;
    },

    fillModal: function() {
      try {
        this.myCollection = window.arangoCollectionsStore.get(this.options.colId).attributes;
      }
      catch (e) {
        // in case the collection cannot be found or something is not present (e.g. after a reload)
        window.App.navigate("#");
        return;
      }

      $('#change-collection-name').val(this.myCollection.name);
      $('#change-collection-id').text(this.myCollection.id);
      $('#change-collection-type').text(this.myCollection.type);
      $('#change-collection-status').text(this.myCollection.status);

      if (this.myCollection.status === 'unloaded') {
        $('#colFooter').prepend(
          '<button id="load-modified-collection" class="button-notification">Load</button>'
        );
        $('#collectionSizeBox').hide();
        $('#collectionSyncBox').hide();
        $('#tab-content-collection-edit tab-pane').css("border-top",0);
      }
      else if (this.myCollection.status === 'loaded') {
        $('#colFooter').prepend(
          '<button id="unload-modified-collection"'+
          'class="button-notification">Unload</button>'
        );
        var data = window.arangoCollectionsStore.getProperties(this.options.colId, true);
        this.fillLoadedModal(data);
      }
    },

    fillLoadedModal: function (data) {

      //show tabs & render figures tab-view
      $('#change-collection .nav-tabs').css("visibility","visible");

      $('#collectionSizeBox').show();
      $('#collectionSyncBox').show();
      if (data.waitForSync === false) {
        $('#change-collection-sync').val('false');
      }
      else {
        $('#change-collection-sync').val('true');
      }
      var calculatedSize = data.journalSize / 1024 / 1024;
      $('#change-collection-size').val(calculatedSize);
      $('#change-collection').modal('show');
    },
    saveModifiedCollection: function() {
      var newname;
      if (this.isCoordinator) {
        newname = this.myCollection.name;
      }
      else {
        newname = $('#change-collection-name').val();
        if (newname === '') {
          arangoHelper.arangoError('No collection name entered!');
          return 0;
        }
      }

      var collid = this.getCollectionId();
      var status = this.getCollectionStatus();

      if (status === 'loaded') {
        var result;
        if (this.myCollection.name !== newname) {
          result = window.arangoCollectionsStore.renameCollection(collid, newname);
        }

        var wfs = $('#change-collection-sync').val();
        var journalSize;
        try {
          journalSize = JSON.parse($('#change-collection-size').val() * 1024 * 1024);
        }
        catch (e) {
          arangoHelper.arangoError('Please enter a valid number');
          return 0;
        }
        var changeResult = window.arangoCollectionsStore.changeCollection(collid, wfs, journalSize);

        if (result !== true) {
          if (result !== undefined) {
            arangoHelper.arangoError("Collection error: " + result);
            return 0;
          }
        }

        if (changeResult !== true) {
          arangoHelper.arangoNotification("Collection error", changeResult);
          return 0;
        }

        if (changeResult === true) {
            window.arangoCollectionsStore.fetch({
              success: function () {
                window.collectionsView.render();
              }
            });
            this.hideModal();
        }
      }
      else if (status === 'unloaded') {
        if (this.myCollection.name !== newname) {
          var result2 = window.arangoCollectionsStore.renameCollection(collid, newname);
          if (result2 === true) {

            window.arangoCollectionsStore.fetch({
              success: function () {
                window.collectionsView.render();
              }
            });
            this.hideModal();
          }
          else {
            arangoHelper.arangoError("Collection error: " + result2);
          }
        }
        else {
          this.hideModal();
        }
      }
    },
    getCollectionId: function () {
      return this.myCollection.id;
    },
    getCollectionStatus: function () {
      return this.myCollection.status;
    },
    unloadCollection: function () {
      var collid = this.getCollectionId();
      window.arangoCollectionsStore.unloadCollection(collid);
      this.hideModal();
    },
    loadCollection: function () {
      var collid = this.getCollectionId();
      window.arangoCollectionsStore.loadCollection(collid);
      this.hideModal();
    },
    hideModal: function () {
      $('#change-collection').modal('hide');
    },
    deleteCollection: function () {
      $('#reallyDeleteColDiv').show();
    },
    abortDeleteCollection: function() {
      $('#reallyDeleteColDiv').hide();
    },
    confirmDeleteCollection: function () {
      var self = this;
      var collName = self.myCollection.name;
      var returnval = window.arangoCollectionsStore.deleteCollection(collName);
      if (returnval === false) {
        arangoHelper.arangoError('Could not delete collection.');
      }
      self.hideModal();
    }

  });
}());
