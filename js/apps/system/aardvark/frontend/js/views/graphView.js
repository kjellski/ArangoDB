/*jslint indent: 2, nomen: true, maxlen: 100, vars: true, white: true, plusplus: true*/
/*global Backbone, $, _, window, templateEngine, GraphViewerUI */
/*global require*/
(function() {

  "use strict";
  
  window.GraphView = Backbone.View.extend({
    el: '#content',

    template: templateEngine.createTemplate("graphView.ejs"),

    initialize: function () {
      this.newLineTmpl = templateEngine.createTemplate("graphViewGroupByEntry.ejs");
      this.graphs = this.options.graphs;
      this.labelId = 1;
      this.colourId = 1;
      this.groupId = 1;
    },

    events: {
      "click input[type='radio'][name='loadtype']": "toggleLoadtypeDisplay",
      "click #createViewer": "createViewer",
      "click #add_label": "insertNewLabelLine",
      "click #add_colour": "insertNewColourLine",
      "click #add_group_by": "insertNewGroupLine",
      "click input[type='radio'][name='colour']": "toggleColourDisplay",
      "click .gv_internal_remove_line": "removeAttrLine",
      "click #manageGraphs": "showGraphManager"
    },

    showGraphManager: function() {
      window.App.navigate("graphManagement", {trigger: true});
    },

    removeAttrLine: function(e) {
      var g = $(e.currentTarget)
        .parent()
        .parent(),
        set = g.parent();
      set.get(0).removeChild(g.get(0));
    }, 

    insertNewLabelLine: function() {
      this.labelId++;
      var next = this.newLineTmpl.render({
        id: this.labelId,
        type: "label"
      });
      $("#label_list").append(next);
    },

    insertNewColourLine: function() {
      this.colourId++;
      var next = this.newLineTmpl.render({
        id: this.colourId,
        type: "colour"
      });
      $("#colour_list").append(next);
    },


    insertNewGroupLine: function() {
      this.groupId++;
      var next = this.newLineTmpl.render({
        id: this.groupId,
        type: "group_by"
      });
      $("#group_by_list").append(next);
    },

    toggleLoadtypeDisplay: function() {
      var selected = $("input[type='radio'][name='loadtype']:checked").attr("id");
      if (selected === "useCollections") {
        $("#collection_config").css("display", "block");
        $("#graph_config").css("display", "none");
      } else {
        $("#collection_config").css("display", "none");
        $("#graph_config").css("display", "block");
      }
    },

    toggleColourDisplay: function() {
      var selected = $("input[type='radio'][name='colour']:checked").attr("id");
      if (selected === "samecolour") {
        $("#colourAttribute_config").css("display", "none");
        return;
      }
      $("#colourAttribute_config").css("display", "block");
    },

    createViewer: function() {
      var ecol,
        ncol,
        aaconfig,
        undirected,
        randomStart,
        groupByAttribute,
        label,
        color,
        config,
        sameColor,
        width,
        graphName,
        graph,
        selected,
        self = this;

      undirected = !!$("#undirected").attr("checked");
      randomStart = !!$("#randomStart").attr("checked");
      
      selected = $("input[type='radio'][name='loadtype']:checked").attr("id");
      if (selected === "useCollections") {
        // selected two individual collections
        ecol = $("#edgeCollection").val();
        ncol = $("#nodeCollection").val();
      }
      else {
        // selected a "graph"
        graphName = $("#graphSelected").val();
        graph = this.graphs.get(graphName);
        if (graph) {
          ecol = graph.get("edges");
          ncol = graph.get("vertices");
        }
      }

      label = [];
      $("#label_list input").each(function() {
        var a = $(this).val();
        if (a) {
          label.push(a);
        }
      });

      sameColor = $("input[type='radio'][name='colour']:checked").attr("id") === "samecolour";
      if (sameColor) {
        color = label;
      } else {
      color = [];
        $("#colour_list input").each(function() {
          var a = $(this).val();
          if (a) {
            color.push(a);
          }
        });
      }

      groupByAttribute = [];
      $("#group_by_list input").each(function() {
        var a = $(this).val();
        if (a) {
          groupByAttribute.push(a);
        }
      });

      aaconfig = {
        type: "arango",
        nodeCollection: ncol,
        edgeCollection: ecol,
        undirected: undirected,
        graph: graphName,
        baseUrl: require("internal").arango.databasePrefix("/")
      };
      
      if (groupByAttribute.length > 0) {
        aaconfig.prioList = groupByAttribute;
      }

      if (label !== undefined && label !== "") {
        config = {
          nodeShaper: {
            label: label
          }
        };
      }
      if (color !== undefined && color !== "") {
        config.nodeShaper = config.nodeShaper || {};
        config.nodeShaper.color = {
          type: "attribute",
          key: color
        };
      }
      width = this.width || $("#content").width();

      $("#background").remove();
      if (randomStart) {
        $.ajax({
          cache: false,
          type: 'PUT',
          url: '/_api/simple/any',
          data: JSON.stringify({
            collection: ncol
          }),
          contentType: "application/json",
          success: function(data) {
            self.ui = new GraphViewerUI($("#content")[0], 
                                        aaconfig, 
                                        width, 
                                        680, 
                                        config, 
                                        (data.document && data.document._id));
          }
        });
      } else {
        self.ui = new GraphViewerUI($("#content")[0], aaconfig, width, 680, config);
      }
    },

    handleResize: function(w) {
      this.width = w;
      if (this.ui) {
        this.ui.changeWidth(w);
      }
    },

    render: function() {
      this.graphs.fetch({async: false});
      $(this.el).html(this.template.render({
        col: this.collection, 
        gs: this.graphs.pluck("_key")
      }));
      delete this.ui;
      return $(this.el);
    }

  });
}());
