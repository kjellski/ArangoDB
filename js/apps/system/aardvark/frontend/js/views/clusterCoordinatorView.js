/*jslint indent: 2, nomen: true, maxlen: 100, vars: true, white: true, plusplus: true*/
/*global Backbone, templateEngine, $, window */

(function() {
  "use strict";

  window.ClusterCoordinatorView = Backbone.View.extend({
    
    el: '#clusterServers',

    template: templateEngine.createTemplate("clusterCoordinatorView.ejs"),

    unrender: function() {
      $(this.el).html("");
    },

    render: function() {
      $(this.el).html(this.template.render({
        coordinators: this.collection.getList()
      }));
      return this;
    }

  });

}());
