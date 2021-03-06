/*jslint indent: 2, nomen: true, maxlen: 100, white: true  plusplus: true, browser: true*/
/*global describe, beforeEach, afterEach, it, spyOn, expect*/
/*global runs, waitsFor, jasmine, waits*/
/*global $, console */
(function() {
  "use strict";
  describe("Collections View", function() {
    var myStore,
      myView,
      div,
      edgeCol,
      docCol,
      sysCol;

    beforeEach(function() {
      div = document.createElement("div");
      div.id = "content";
      document.body.appendChild(div);
      edgeCol = new window.arangoCollectionModel({
        id: "e",
        type: "edge",
        isSystem: false,
        name: "e",
        status: "loaded"
      });
      docCol = new window.arangoCollectionModel({
        id: "d",
        type: "document",
        isSystem: false,
        name: "d",
        status: "loaded"
      });
      sysCol = new window.arangoCollectionModel({
        id: "s",
        type: "document",
        isSystem: true,
        name: "_sys",
        status: "loaded"
      });


      var cols = [edgeCol, docCol, sysCol];  
      spyOn($, "ajax").andCallFake(function(url) {
        console.log(url);
      });
      myStore = new window.arangoCollections(cols);
      myView = new window.CollectionsView({
        collection: myStore
      });

    });

    afterEach(function() {
      document.body.removeChild(div);
    });

    describe("Check Collection Display Settings", function() {

      it("Check if System Collections should not be drawn", function() {
        myView.render();
        var wasRendered;

        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = false;
        });
        myView.checkSystem();
        expect(wasRendered).toBeFalsy();
      });

      it("Check if System Collections should be drawn", function() {
        myView.render();
        var wasRendered;

        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = true;
        });
        $('#checkSystem').click();
        myView.checkSystem();
        expect(wasRendered).toBeTruthy();
      });

      it("Check if Edge Collections should not be drawn", function() {
        myView.render();
        var wasRendered;

        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = false;
        });
        myView.checkEdge();
        expect(wasRendered).toBeFalsy();
      });

      it("Check if Edge Collections should be drawn", function() {
        myView.render();
        var wasRendered;
        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = true;
        });

        $('#checkEdge').click();
        myView.checkEdge();
        expect(wasRendered).toBeTruthy();
      });
      
      it("Check if Document Collections should not be drawn", function() {
        myView.render();
        var wasRendered;

        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = false;
        });
        myView.checkDocument();
        expect(wasRendered).toBeFalsy();
      });

      it("Check if Document Collections should be drawn", function() {
        myView.render();
        var wasRendered;
        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = true;
        });

        $('#checkDocument').click();
        myView.checkDocument();
        expect(wasRendered).toBeTruthy();
      });

      it("Check if Loaded Collections should not be drawn", function() {
        myView.render();
        var wasRendered;

        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = false;
        });
        myView.checkLoaded();
        expect(wasRendered).toBeFalsy();
      });

      it("Check if Loaded Collections should be drawn", function() {
        myView.render();
        var wasRendered;
        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = true;
        });

        $('#checkLoaded').click();
        myView.checkLoaded();
        expect(wasRendered).toBeTruthy();
      });

      it("Check if Unloaded Collections should not be drawn", function() {
        myView.render();
        var wasRendered;

        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = false;
        });
        myView.checkUnloaded();
        expect(wasRendered).toBeFalsy();
      });

      it("Check if Unloaded Collections should be drawn", function() {
        myView.render();
        var wasRendered;
        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = true;
        });

        $('#checkUnloaded').click();
        expect(wasRendered).toBeTruthy();
        wasRendered =false;
        myView.checkUnloaded();
        expect(wasRendered).toBeFalsy();
      });

      it("Check if Name Sort is inactive", function() {
        myView.render();
        var wasRendered;

        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = false;
        });
        myView.sortName();
        expect(wasRendered).toBeFalsy();
      });

      it("Check if Type is inactive", function() {
        myView.render();
        var wasRendered;

        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = false;
        });
        myView.sortType();
        expect(wasRendered).toBeFalsy();
      });

      it("Check if sortOrder is inactive", function() {
        myView.render();
        var wasRendered;

        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = false;
        });
        myView.sortOrder();
        expect(wasRendered).toBeFalsy();
      });

      it("Check Filter Values", function() {
        myView.render();
        var check = false;
        $.each(myView.collection.searchOptions, function() {
          if (this === undefined) {
            check = false;
            return;
          }
          check = true;
        });
        expect(check).toBeTruthy();
      });
    });

    describe("Check Collection Search Functions", function() {

      it("Check if search needs to be executed (false)", function() {
        myView.render();
        var wasRendered = false;
        myView.search();

        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = true;
        });

        expect(wasRendered).toBeFalsy();
      });

      it("Check if search needs to be executed (true)", function() {
        myView.render();
        var wasRendered = false;

        spyOn(myView, 'render').andCallFake(function(request) {
          wasRendered = true;
        });

        $('#searchInput').val("HalloPeter");
        myView.collection.searchOptions.searchPhrase = "HalloMax";
        myView.search();

        expect(wasRendered).toBeTruthy();
      });

      it("Reset search phrase", function() {
        myView.render();
        myView.resetSearch();
        expect(myView.collection.searchOptions.searchPhrase).toBe(null);
      });

      it("Reset search timeout", function() {
        myView.render();
        myView.searchTimeout = 1;
        myView.resetSearch();
        expect(myView.searchTimeout).toBe(null);
      });

      it("Check restrictToSearchPhraseKey function", function() {
        myView.render();
        var resetSearch, newSearch;

        spyOn(myView, 'resetSearch').andCallFake(function(request) {
          resetSearch = true;
        });

        spyOn(myView, 'search').andCallFake(function(request) {
          newSearch = true;
        });

        runs(function() {
          myView.restrictToSearchPhraseKey();
        });
          waits(300);
        runs(function() {
            expect(resetSearch).toBeTruthy();
            expect(newSearch).toBeTruthy();
        });
      });


    });

  });
}());
