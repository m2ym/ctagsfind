;;; ctagsfind.el --- ctagsfind client for GNU Emacs

;; Copyright (C) 2010  Tomohiro Matsuyama

;; Author: Tomohiro Matsuyama <tomo@cx4a.org>
;; Keywords: convenience

;; This program is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.

;; This program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with this program.  If not, see <http://www.gnu.org/licenses/>.

;;; Commentary:

;; 

;;; Code:

(require 'thingatpt)

(defgroup ctagsfind nil
  "Ctagsfind."
  :group 'convenience
  :prefix "ctagsfind-")

(defcustom ctagsfind-program "ctagsfind"
  "Path to ctagsfind program."
  :type 'string
  :group 'ctagsfind)

(defvar ctagsfind-stack nil)
(defvar ctagsfind-current-tags nil)
(defvar ctagsfind-last-tag nil)
(defvar ctagsfind-last-tag-index nil)
(defvar ctagsfind-last-tag-name nil)
(defvar ctagsfind-find-tag-history nil)

(defun ctagsfind-tag-name-at-point ()
  "Return tag name at point."
  (let ((bounds (bounds-of-thing-at-point 'symbol)))
    (if bounds
        (buffer-substring-no-properties (car bounds) (cdr bounds)))))

(defun ctagsfind-devide-string (string separators)
  "Devide `STRING` into two parts with `SEPARATORS`."
  (if (string-match separators string)
      (list (substring string 0 (match-beginning 0))
            (substring string (match-end 0)))
    (list string)))

(defun ctagsfind-vim-regexp-to-emacs-regexp (regexp)
  (replace-regexp-in-string "[*+]" "\\\\\\&" regexp))

(defun ctagsfind-command-to-string (command)
  "Execute `COMMAND` and return the output string.
`COMMAND` must be (PROGRAM ARGS...)."
  (with-output-to-string
    (with-current-buffer standard-output
      (apply 'call-process (car command) nil t nil (cdr command)))))

(defun ctagsfind-make-tag (name path address)
  (list name path address))

(defun ctagsfind-lookup (name &rest field-plist)
  "Lookup tags with `NAME' and return them."
  (let (args)
    (while (and field-plist (cdr field-plist))
      (push "-F" args)
      (push (format "%s=%s" (car field-plist) (cadr field-plist)) args)
      (setq field-plist (cddr field-plist)))
    (delq nil
          (mapcar (lambda (line)
                    (let* ((fields (ctagsfind-devide-string line "\t"))
                           (path (car fields))
                           (address (cadr fields)))
                      (if (and path address)
                          (ctagsfind-make-tag name path address))))
                  (split-string (ctagsfind-command-to-string `(,ctagsfind-program ,@(nreverse args) ,name)) "\n")))))

(defun ctagsfind-jump (tag)
  (let ((path (nth 1 tag))
        (address (nth 2 tag)))
    (if (condition-case nil
            (progn
              (find-file path)
              (cond
               ((string-match "^[0-9]+$" address)
                (goto-line (string-to-number address)))
               ((string-match "^/\\(.+\\)/$" address)
                (goto-char (point-min))
                (re-search-forward (ctagsfind-vim-regexp-to-emacs-regexp (match-string 1 address)) nil t)
                (back-to-indentation))
               ((string-match "^\\?\\(.+\\)\\?$" address)
                (goto-char (point-max))
                (re-search-backward (ctagsfind-vim-regexp-to-emacs-regexp (match-string 1 address)) nil t)
                (back-to-indentation))
               (t
                (error "Invalid address")))
              (setq ctagsfind-last-tag tag)
              t)
          (error . nil))
        t
      (message "Can't find tag: %s" tag)
      nil)))

(defun ctagsfind-jump-and-push (tag)
  (let ((marker (point-marker)))
    (if (ctagsfind-jump tag)
        (push marker ctagsfind-stack))))

(defun ctagsfind-start (tags)
  (setq ctagsfind-current-tags tags
        ctagsfind-last-tag nil
        ctagsfind-last-tag-index nil
        ctagsfind-last-tag-name name))

(defun ctagsfind-find-next ()
  "Find next tag with last tag name."
  (interactive)
  (unless ctagsfind-current-tags
    (error "No tags found"))
  (setq ctagsfind-last-tag-index
        (if (or (null ctagsfind-last-tag-index)
                (>= ctagsfind-last-tag-index (1- (length ctagsfind-current-tags))))
            0
          (1+ ctagsfind-last-tag-index)))
  (ctagsfind-jump-and-push (nth ctagsfind-last-tag-index ctagsfind-current-tags)))

(defun ctagsfind-find-previous ()
  "Find previous tag with last tag name."
  (interactive)
  (unless ctagsfind-current-tags
    (error "No tags found"))
  (setq ctagsfind-last-tag-index
        (if (or (null ctagsfind-last-tag-index)
                (eq ctagsfind-last-tag-index 0))
            (1- (length ctagsfind-current-tags))
          (1- ctagsfind-last-tag-index)))
  (ctagsfind-jump-and-push (nth ctagsfind-last-tag-index ctagsfind-current-tags)))

(defun ctagsfind-find-tag-interactive ()
  (if current-prefix-arg
      (list nil current-prefix-arg)
    (let* ((default (ctagsfind-tag-name-at-point))
           (prompt (if default
                       (format "Find tag (%s): " default)
                     "Find tag: ")))
      (list (read-string prompt nil 'ctagsfind-history default) nil))))

(defun ctagsfind-find-tag (name next-p)
  "Find tag with specified `NAME'. With positive prefix,
find next tag with last tag name. With negative prefix,
find previous tag with last tag name."
  (interactive (ctagsfind-find-tag-interactive))
  (cond
   ((or (eq next-p '-)
        (and (numberp next-p)
             (< next-p 0)))
    (ctagsfind-find-previous))
   (next-p
    (ctagsfind-find-next))
   (t
    (ctagsfind-start (ctagsfind-lookup name))
    (ctagsfind-find-next))))

(defun ctagsfind-pop-tag ()
  "Pop back to last jumped tag."
  (interactive)
  (unless ctagsfind-stack
    (error "Tag stack is empty"))
  (let ((marker (pop ctagsfind-stack)))
    (switch-to-buffer (or (marker-buffer marker)
                          (error "The marked buffer has been deleted")))
    (goto-char (marker-position marker))
    (set-marker marker nil nil)))

(provide 'ctagsfind)
;;; ctagsfind.el ends here
