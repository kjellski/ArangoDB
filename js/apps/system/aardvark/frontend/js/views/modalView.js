/*jslint indent: 2, nomen: true, maxlen: 100, vars: true, white: true, plusplus: true */
/*global Backbone, $, window, _ */
/*global templateEngine*/

(function () {
  "use strict";

  var createButtonStub = function(type, title, cb) {
    return {
      type: type,
      title: title,
      callback: cb
    };
  };

  var createTextStub = function(type, label, value, info) {
    var obj = {
      type: type,
      label: label
    };
    if (value) {
      obj.value = value;
    }
    if (info) {
      obj.info = info;
    }
    return obj;
  };

  window.ModalView = Backbone.View.extend({

    baseTemplate: templateEngine.createTemplate("modalBase.ejs"),
    tableTemplate: templateEngine.createTemplate("modalTable.ejs"),
    el: "#modalPlaceholder",
    contentEl: "#modalContent",
    confirm: {
      list: "#modal-delete-confirmation",
      yes: "#modal-confirm-delete",
      no: "#modal-abort-delete"
    },

    buttons: {
      SUCCESS: "success",
      NOTIFICATION: "notification",
      DELETE: "danger",
      NEUTRAL: "neutral",
      CLOSE: "close"
    },
    tables: {
      READONLY: "readonly",
      TEXT: "text",
      PASSWORD: "password",
      SELECT: "select",
      SELECT2: "select2",
      CHECKBOX: "checkbox"
    },
    closeButton: {
      type: "close",
      title: "Cancel"
    },

    initialize: function() {
      Object.freeze(this.buttons);
      Object.freeze(this.tables);
      var self = this;
      this.closeButton.callback = function() {
        self.hide(); 
      };
      //this.hide.bind(this);
    },

    createCloseButton: function(cb) {
      var self = this;
      return createButtonStub(this.buttons.CLOSE, this.closeButton.title, function () {
          self.closeButton.callback();
          cb();
      });
    },


    createSuccessButton: function(title, cb) {
      return createButtonStub(this.buttons.SUCCESS, title, cb);
    },

    createNotificationButton: function(title, cb) {
      return createButtonStub(this.buttons.NOTIFICATION, title, cb);
    },

    createDeleteButton: function(title, cb) {
      return createButtonStub(this.buttons.DELETE, title, cb);
    },

    createNeutralButton: function(title, cb) {
      return createButtonStub(this.buttons.NEUTRAL, title, cb);
    },

    createDisabledButton: function(title) {
      var disabledButton = createButtonStub(this.buttons.NEUTRAL, title);
      disabledButton.disabled = true;
      return disabledButton;
    },

    createReadOnlyEntry: function(id, label, value, info) {
      var obj = createTextStub(this.tables.READONLY, label, value, info);
      obj.id = id;
      return obj;
    },

    createTextEntry: function(id, label, value, info, placeholder, mandatory) {
      var obj = createTextStub(this.tables.TEXT, label, value, info);
      obj.id = id;
      if (placeholder) {
        obj.placeholder = placeholder;
      }
      if (mandatory) {
        obj.mandatory = mandatory;
      }
      return obj;
    },

    createSelect2Entry: function(id, label, value, info, placeholder, mandatory) {
      var obj = createTextStub(this.tables.SELECT2, label, value, info);
      obj.id = id;
      if (placeholder) {
        obj.placeholder = placeholder;
      }
      if (mandatory) {
        obj.mandatory = mandatory;
      }
      return obj;
    },

    createPasswordEntry: function(id, label, value, info, placeholder, mandatory) {
      var obj = createTextStub(this.tables.PASSWORD, label, value, info);
      obj.id = id;
      if (placeholder) {
        obj.placeholder = placeholder;
      }
      if (mandatory) {
        obj.mandatory = mandatory;
      }
      return obj;
    },

    createCheckboxEntry: function(id, label, value, info, checked) {
      var obj = createTextStub(this.tables.CHECKBOX, label, value, info);
      obj.id = id;
      if (checked) {
        obj.checked = checked;
      }
      return obj;
    },

    createSelectEntry: function(id, label, selected, info, options) {
      var obj = createTextStub(this.tables.SELECT, label, null, info);
      obj.id = id;
      if (selected) {
        obj.selected = selected;
      }
      obj.options = options;
      return obj;
    },

    createOptionEntry: function(label, value) {
      return {
        label: label,
        value: value || label
      };
    },

    show: function(templateName, title, buttons, tableContent, advancedContent) {
      var self = this, lastBtn, closeButtonFound = false;
      buttons = buttons || [];
      // Insert close as second from right
      if (buttons.length > 0) {
        buttons.forEach(function (b) {
            if (b.type === self.buttons.CLOSE) {
                closeButtonFound = true;
            }
        });
        if (!closeButtonFound) {
            lastBtn = buttons.pop();
            buttons.push(self.closeButton);
            buttons.push(lastBtn);
        }
      } else {
        buttons.push(this.closeButton);
      }
      $(this.el).html(this.baseTemplate.render({
        title: title,
        buttons: buttons
      }));
      _.each(buttons, function(b, i) {
        if (b.disabled || !b.callback) {
          return;
        }
        if (b.type === self.buttons.DELETE) {
          $("#modalButton" + i).bind("click", function() {
            $(self.confirm.yes).unbind("click");
            $(self.confirm.yes).bind("click", b.callback);
            $(self.confirm.list).css("display", "block");
          });
          return;  
        }
        $("#modalButton" + i).bind("click", b.callback);
      });
      $(this.confirm.no).bind("click", function() {
        $(self.confirm.list).css("display", "none");
      });

      var template = templateEngine.createTemplate(templateName),
        model = {};
      model.content = tableContent || [];
      model.advancedContent = advancedContent || false;
      $(".modal-body").html(template.render(model));
      $('.modalTooltips').tooltip({
        placement: "left"
      });
      var ind = buttons.indexOf(this.closeButton);
      buttons.splice(ind, 1);

      //handle select2
      _.each(tableContent, function(r) {
        if (r.type === self.tables.SELECT2) {
          $('#'+r.id).select2({
            tags: [],
            showSearchBox: false,
            minimumResultsForSearch: -1,
            width: "336px",
            maximumSelectionSize: 8
          });
        }
      });//handle select2

      $("#modal-dialog").modal("show");
    },

    hide: function() {
      $("#modal-dialog").modal("hide");
    }
  });
}());
