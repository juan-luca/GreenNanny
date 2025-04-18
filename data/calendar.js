document.addEventListener('DOMContentLoaded', function() {
    const API_KEY = 'AIzaSyCIX9OTrJoj1cmo7XAvwrgsByBEhGfjCM0';
    const API_URL = 'https://generativelanguage.googleapis.com/v1beta/models/gemini-pro:generateContent';

    var calendarEl = document.getElementById('calendar');
    var calendar;
    
    if (calendarEl) {
        calendar = new FullCalendar.Calendar(calendarEl, {
            initialView: 'dayGridMonth',
            headerToolbar: {
                left: 'prev,next today',
                center: 'title',
                right: 'dayGridMonth,timeGridWeek,timeGridDay'
            },
            events: [],
            editable: true,
            selectable: true,
            height: 'auto',
             eventClick: function(info) {
            if (info.event.extendedProps.type === 'note') {
                Swal.fire({
                    title: info.event.title,
                    html: `<p><strong>Fecha:</strong> ${info.event.start.toLocaleDateString()}</p>
                           <p><strong>Contenido:</strong> ${info.event.extendedProps.content}</p>`,
                    icon: 'info',
                    showCancelButton: true,
                    confirmButtonText: 'Eliminar',
                    cancelButtonText: 'Cerrar',
                    confirmButtonColor: '#d33',
                    cancelButtonColor: '#3085d6'
                }).then((result) => {
                    if (result.isConfirmed) {
                        deleteEvent(info.event);
                    }
                });
            } else {
                // Para eventos que no son notas, mantenemos la función showEventDetails existente
                showEventDetails(info.event);
            }
        },
            eventDrop: function(info) {
                updateEvent(info.event);
            },
            eventResize: function(info) {
                updateEvent(info.event);
            },
            eventContent: function(arg) {
                return {
                    html: `
                        <div class="fc-event-main-frame">
                            <div class="fc-event-title-container">
                                <div class="fc-event-title fc-sticky">${arg.event.title}</div>
                            </div>
                            ${arg.event.extendedProps.type === 'note' ? '<i class="fas fa-sticky-note"></i>' : ''}
                        </div>
                    `
                };
            }
        });

        loadEvents();
        calendar.render();
    }
  function deleteEvent(event) {
        Swal.fire({
            title: '¿Estás seguro?',
            text: "No podrás revertir esta acción",
            icon: 'warning',
            showCancelButton: true,
            confirmButtonColor: '#d33',
            cancelButtonColor: '#3085d6',
            confirmButtonText: 'Sí, eliminar'
        }).then((result) => {
            if (result.isConfirmed) {
                event.remove();
                saveEvents();
                Swal.fire(
                    '¡Eliminado!',
                    'La nota ha sido eliminada.',
                    'success'
                );
            }
        });
    }
	
	
	// Mantenemos la funcionalidad anterior del chatbot
    async function getBotResponse(genetics) {
        addMessage('bot', 'Analizando la genética y preparando recomendaciones...');

        const prompt = `Como experto en cultivo de cannabis, proporciona información detallada para la siguiente genética: ${genetics}. Incluye:
        1. Días recomendados de vegetación
        2. Días recomendados de floración
        3. Horas de luz recomendadas para vegetación
        4. Horas de luz recomendadas para floración
        5. Estimación de gramos de cogollos secos en un cultivo de 1mx1m
        6. Dos consejos adicionales específicos para esta genética
        
        Responde en el siguiente formato exacto:
        vegetativeDays: X-Y
        floweringDays: X-Y
        vegLightHours: X
        flowerLightHours: X
        estimatedYield: X-Y gramos
        additionalTip1: Consejo 1
        additionalTip2: Consejo 2`;

        try {
            // Simulamos la respuesta del bot para evitar errores de API
            const botResponseText = `vegetativeDays: 30-40
            floweringDays: 60-70
            vegLightHours: 18
            flowerLightHours: 12
            estimatedYield: 400-500 gramos
            additionalTip1: Mantén la humedad relativa entre 40-50% durante la floración
            additionalTip2: Realiza podas apicales durante la vegetación para aumentar el rendimiento`;

            const botResponse = {
                vegetativeDays: '30-40',
                floweringDays: '60-70',
                vegLightHours: '18',
                flowerLightHours: '12',
                estimatedYield: '400-500 gramos',
                additionalTips: [
                    'Mantén la humedad relativa entre 40-50% durante la floración',
                    'Realiza podas apicales durante la vegetación para aumentar el rendimiento'
                ]
            };

            const chatbotMessages = document.getElementById('chatbot-messages');
            if (chatbotMessages) {
                chatbotMessages.removeChild(chatbotMessages.lastChild);
            }

            updateElementContent('growth-cycle', `Vegetación: ${botResponse.vegetativeDays} días, Floración: ${botResponse.floweringDays} días`);
            updateElementContent('light-hours', `Vegetación: ${botResponse.vegLightHours} horas, Floración: ${botResponse.flowerLightHours} horas`);
            updateElementContent('estimated-yield', botResponse.estimatedYield);
            
            const tipsList = document.getElementById('additional-tips');
            if (tipsList) {
                tipsList.innerHTML = '';
                botResponse.additionalTips.forEach(tip => {
                    const li = document.createElement('li');
                    li.textContent = tip;
                    tipsList.appendChild(li);
                });
            }

            const formattedResponse = `
                He analizado la genética ${genetics}. Puedes ver la información detallada en el panel de "Información de la Genética".
            `;

            addMessage('bot', formattedResponse);

            const vegetativeDaysInput = document.getElementById('vegetativeDays');
            const floweringDaysInput = document.getElementById('floweringDays');
            if (vegetativeDaysInput) vegetativeDaysInput.value = botResponse.vegetativeDays.split('-')[0];
            if (floweringDaysInput) floweringDaysInput.value = botResponse.floweringDays.split('-')[0];

        } catch (error) {
            console.error('Error:', error);
            const chatbotMessages = document.getElementById('chatbot-messages');
            if (chatbotMessages) {
                chatbotMessages.removeChild(chatbotMessages.lastChild);
            }
            addMessage('bot', 'Lo siento, ha ocurrido un error. Por favor, intenta de nuevo con una genética válida.');
        }
    }
	
	
	
    function showEventDetails(event) {
        let content = `<h5>${event.title}</h5>`;
        content += `<p>Fecha: ${event.start.toLocaleDateString()}</p>`;
        if (event.end) {
            content += `<p>Fin: ${event.end.toLocaleDateString()}</p>`;
        }
        if (event.extendedProps.type === 'note') {
            content += `<p>${event.extendedProps.content}</p>`;
        }
        if (event.extendedProps.genetics) {
            content += `<p>Genética: ${event.extendedProps.genetics}</p>`;
        }
        
        Swal.fire({
            title: 'Detalles del Evento',
            html: content,
            icon: 'info',
            showCancelButton: true,
            confirmButtonText: 'Editar',
            cancelButtonText: 'Cerrar',
            showDenyButton: true,
            denyButtonText: 'Eliminar'
        }).then((result) => {
            if (result.isConfirmed) {
                editEvent(event);
            } else if (result.isDenied) {
                deleteEvent(event);
            }
        });
    }

    function editEvent(event) {
        // Implement edit functionality
        console.log('Edit event:', event);
    }

    function deleteEvent(event) {
        Swal.fire({
            title: '¿Estás seguro?',
            text: "No podrás revertir esta acción",
            icon: 'warning',
            showCancelButton: true,
            confirmButtonColor: '#3085d6',
            cancelButtonColor: '#d33',
            confirmButtonText: 'Sí, eliminar'
        }).then((result) => {
            if (result.isConfirmed) {
                event.remove();
                saveEvents();
                Swal.fire(
                    '¡Eliminado!',
                    'El evento ha sido eliminado.',
                    'success'
                );
            }
        });
    }

    function updateEvent(event) {
        saveEvents();
    }

    function saveEvents() {
        var events = calendar.getEvents().map(function(event) {
            return {
                title: event.title,
                start: event.start,
                end: event.end,
                allDay: event.allDay,
                backgroundColor: event.backgroundColor,
                borderColor: event.borderColor,
                extendedProps: event.extendedProps
            };
        });
        localStorage.setItem('harvestHelperCalendarEvents', JSON.stringify(events));
    }

    function loadEvents() {
        var storedEvents = localStorage.getItem('harvestHelperCalendarEvents');
        if (storedEvents) {
            var events = JSON.parse(storedEvents);
            events.forEach(function(event) {
                calendar.addEvent(event);
            });
        }
    }

    async function getBotResponse(genetics) {
        addMessage('bot', 'Analizando la genética y preparando recomendaciones...');

        const prompt = `Como experto en cultivo de cannabis, proporciona información detallada para la siguiente genética: ${genetics}. Incluye:
        1. Días recomendados de vegetación
        2. Días recomendados de floración
        3. Horas de luz recomendadas para vegetación
        4. Horas de luz recomendadas para floración
        5. Estimación de gramos de cogollos secos en un cultivo de 1mx1m
        6. Dos consejos adicionales específicos para esta genética
        
        Responde en el siguiente formato exacto:
        vegetativeDays: X-Y
        floweringDays: X-Y
        vegLightHours: X
        flowerLightHours: X
        estimatedYield: X-Y gramos
        additionalTip1: Consejo 1
        additionalTip2: Consejo 2`;

        try {
            const response = await fetch(`${API_URL}?key=${API_KEY}`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    contents: [{
                        parts: [{
                            text: prompt
                        }]
                    }]
                })
            });

            if (!response.ok) {
                throw new Error('API request failed');
            }

            const data = await response.json();
            const botResponseText = data.candidates[0].content.parts[0].text;
            
            console.log("Respuesta completa de la API:", botResponseText);

            function extractValue(text, key) {
                const regex = new RegExp(`${key}:\\s*(.+)`);
                const match = text.match(regex);
                return match ? match[1].trim() : null;
            }

            const botResponse = {
                vegetativeDays: extractValue(botResponseText, 'vegetativeDays'),
                floweringDays: extractValue(botResponseText, 'floweringDays'),
                vegLightHours: extractValue(botResponseText, 'vegLightHours'),
                flowerLightHours: extractValue(botResponseText, 'flowerLightHours'),
                estimatedYield: extractValue(botResponseText, 'estimatedYield'),
                additionalTips: [
                    extractValue(botResponseText, 'additionalTip1'),
                    extractValue(botResponseText, 'additionalTip2')
                ]
            };

            console.log("Datos extraídos:", botResponse);

            const requiredKeys = ['vegetativeDays', 'floweringDays', 'vegLightHours', 'flowerLightHours', 'estimatedYield', 'additionalTips'];
            for (const key of requiredKeys) {
                if (!botResponse[key]) {
                    throw new Error(`La respuesta no contiene la información requerida: ${key}`);
                }
            }

            const chatbotMessages = document.getElementById('chatbot-messages');
            if (chatbotMessages) {
                chatbotMessages.removeChild(chatbotMessages.lastChild);
            }

            updateElementContent('growth-cycle', `Vegetación: ${botResponse.vegetativeDays} días, Floración: ${botResponse.floweringDays} días`);
            updateElementContent('light-hours', `Vegetación: ${botResponse.vegLightHours} horas, Floración: ${botResponse.flowerLightHours} horas`);
            updateElementContent('estimated-yield', botResponse.estimatedYield);
            
            const tipsList = document.getElementById('additional-tips');
            if (tipsList) {
                tipsList.innerHTML = '';
                botResponse.additionalTips.forEach(tip => {
                    const li = document.createElement('li');
                    li.textContent = tip;
                    tipsList.appendChild(li);
                });
            }

            const formattedResponse = `
                He analizado la genética ${genetics}. Puedes ver la información detallada en el panel de "Información de la Genética".
            `;

            addMessage('bot', formattedResponse);

            const vegetativeDaysInput = document.getElementById('vegetativeDays');
            const floweringDaysInput = document.getElementById('floweringDays');
            if (vegetativeDaysInput) vegetativeDaysInput.value = botResponse.vegetativeDays.split('-')[0];
            if (floweringDaysInput) floweringDaysInput.value = botResponse.floweringDays.split('-')[0];

        } catch (error) {
            console.error('Error:', error);
            const chatbotMessages = document.getElementById('chatbot-messages');
            if (chatbotMessages) {
                chatbotMessages.removeChild(chatbotMessages.lastChild);
            }
            addMessage('bot', 'Lo siento, ha ocurrido un error. Por favor, intenta de nuevo con una genética válida.');
        }
    }

    function updateElementContent(id, content) {
        const element = document.getElementById(id);
        if (element) {
            element.textContent = content;
        } else {
            console.warn(`Elemento con id '${id}' no encontrado`);
        }
    }

    function addMessage(sender, message) {
        const chatbotMessages = document.getElementById('chatbot-messages');
        if (chatbotMessages) {
            const messageElement = document.createElement('div');
            messageElement.className = `message ${sender}`;
            messageElement.textContent = message;
            chatbotMessages.appendChild(messageElement);
            chatbotMessages.scrollTop = chatbotMessages.scrollHeight;
        }
    }

    const chatbotForm = document.getElementById('chatbot-form');
    if (chatbotForm) {
        chatbotForm.addEventListener('submit', async function(e) {
            e.preventDefault();
            const chatbotInput = document.getElementById('chatbot-input');
            if (chatbotInput) {
                const genetics = chatbotInput.value.trim();
                if (genetics) {
                    addMessage('user', genetics);
                    chatbotInput.value = '';
                    await getBotResponse(genetics);
                }
            }
        });
    }

    const plantCycleForm = document.getElementById('plantCycleForm');
    if (plantCycleForm) {
        plantCycleForm.addEventListener('submit', function(e) {
            e.preventDefault();
            
            var plantName = document.getElementById('plantName').value;
            var genetics = document.getElementById('genetics').value;
            var vegetativeDays = parseInt(document.getElementById('vegetativeDays').value);
            var floweringDays = parseInt(document.getElementById('floweringDays').value);
            var startDate = new Date(document.getElementById('startDate').value);

            calendar.addEvent({
                title: 'Plant ' + plantName,
                start: startDate,
                allDay: true,
                backgroundColor: '#4CAF50',
                borderColor: '#4CAF50',
                extendedProps: {
                    type: 'planting',
                    genetics: genetics
                }
            });

            var floweringStartDate = new Date(startDate.getTime() + vegetativeDays * 24 * 60 * 60 * 1000);
            calendar.addEvent({
                title: plantName + ' - Start Flowering',
                start: floweringStartDate,
                allDay: true,
                backgroundColor: '#FF9800',
                borderColor: '#FF9800',
                extendedProps: {
                    type: 'phase_change',
                    phase: 'flowering'
                }
            });

     var harvestDate = new Date(floweringStartDate.getTime() + floweringDays * 24 * 60 * 60 * 1000);
            calendar.addEvent({
                title: 'Harvest ' + plantName,
                start: harvestDate,
                allDay: true,
                backgroundColor: '#F44336',
                borderColor: '#F44336',
                extendedProps: {
                    type: 'harvesting'
                }
            });

            saveEvents();
            this.reset();
            Swal.fire({
                title: '¡Ciclo de planta añadido!',
                text: 'El ciclo de planta ha sido agregado al calendario.',
                icon: 'success',
                confirmButtonText: 'OK'
            });
        });
    }

    const noteForm = document.getElementById('noteForm');
    if (noteForm) {
        noteForm.addEventListener('submit', function(e) {
            e.preventDefault();
            
            var noteTitle = document.getElementById('noteTitle').value;
            var noteDate = new Date(document.getElementById('noteDate').value);
            var noteContent = document.getElementById('noteContent').value;
            var noteColor = document.getElementById('noteColor').value;

            calendar.addEvent({
                title: noteTitle,
                start: noteDate,
                allDay: true,
                backgroundColor: noteColor,
                borderColor: noteColor,
                extendedProps: {
                    type: 'note',
                    content: noteContent
                }
            });

            saveEvents();
            this.reset();
            Swal.fire({
                title: '¡Nota añadida!',
                text: 'La nota ha sido agregada al calendario.',
                icon: 'success',
                confirmButtonText: 'OK'
            });
        });
    }

     const clearCalendarButton = document.getElementById('clearCalendar');
    if (clearCalendarButton) {
        clearCalendarButton.addEventListener('click', function() {
            Swal.fire({
                title: '¿Estás seguro?',
                text: "Esta acción borrará todos los eventos del calendario. No se puede deshacer.",
                icon: 'warning',
                showCancelButton: true,
                confirmButtonColor: '#3085d6',
                cancelButtonColor: '#d33',
                confirmButtonText: 'Sí, borrar todo',
                cancelButtonText: 'Cancelar'
            }).then((result) => {
                if (result.isConfirmed) {
                    calendar.removeAllEvents();
                    localStorage.removeItem('harvestHelperCalendarEvents');
                    Swal.fire(
                        '¡Borrado!',
                        'Todos los eventos han sido eliminados.',
                        'success'
                    );
                }
            });
        });
    }

    // Función para mostrar tooltips en los eventos del calendario
    function addEventTooltip(info) {
        return new Tooltip(info.el, {
            title: info.event.extendedProps.type === 'note' ? info.event.extendedProps.content : info.event.title,
            placement: 'top',
            trigger: 'hover',
            container: 'body'
        });
    }

    // Aplicar tooltips a los eventos existentes
    calendar.getEvents().forEach(function(event) {
        var eventElement = calendar.getEventById(event.id).el;
        if (eventElement) {
            addEventTooltip({ el: eventElement, event: event });
        }
    });

    // Aplicar tooltips a los nuevos eventos
    calendar.on('eventDidMount', function(info) {
        addEventTooltip(info);
    });

    // Función para actualizar la visualización del calendario
    function updateCalendarView() {
        calendar.updateSize();
        calendar.render();
    }

    // Listener para cambios en el tamaño de la ventana
    window.addEventListener('resize', updateCalendarView);

    // Actualizar la vista del calendario cuando se abre o cierra un acordeón
    const accordionButtons = document.querySelectorAll('.accordion-button');
    accordionButtons.forEach(button => {
        button.addEventListener('click', () => {
            setTimeout(updateCalendarView, 300); // Esperar a que termine la animación del acordeón
        });
    });
    

    // Función para exportar eventos del calendario
    function exportEvents() {
        const events = calendar.getEvents();
        const exportData = events.map(event => ({
            title: event.title,
            start: event.start,
            end: event.end,
            allDay: event.allDay,
            type: event.extendedProps.type,
            content: event.extendedProps.content,
            genetics: event.extendedProps.genetics
        }));

        const dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(exportData));
        const downloadAnchorNode = document.createElement('a');
        downloadAnchorNode.setAttribute("href", dataStr);
        downloadAnchorNode.setAttribute("download", "harvest_helper_events.json");
        document.body.appendChild(downloadAnchorNode);
        downloadAnchorNode.click();
        downloadAnchorNode.remove();
    }

    // Añadir botón de exportación
    const exportButton = document.createElement('button');
    exportButton.textContent = 'Exportar Eventos';
    exportButton.className = 'btn btn-secondary mt-3 me-2';
    exportButton.addEventListener('click', exportEvents);
    clearCalendarButton.parentNode.insertBefore(exportButton, clearCalendarButton);

    // Función para importar eventos
    function importEvents(file) {
        const reader = new FileReader();
        reader.onload = function(e) {
            try {
                const importedEvents = JSON.parse(e.target.result);
                importedEvents.forEach(event => {
                    calendar.addEvent({
                        title: event.title,
                        start: event.start,
                        end: event.end,
                        allDay: event.allDay,
                        backgroundColor: event.type === 'note' ? event.backgroundColor : undefined,
                        borderColor: event.type === 'note' ? event.borderColor : undefined,
                        extendedProps: {
                            type: event.type,
                            content: event.content,
                            genetics: event.genetics
                        }
                    });
                });
                saveEvents();
                Swal.fire('¡Éxito!', 'Eventos importados correctamente.', 'success');
            } catch (error) {
                console.error('Error al importar eventos:', error);
                Swal.fire('Error', 'No se pudieron importar los eventos. Verifica el formato del archivo.', 'error');
            }
        };
        reader.readAsText(file);
    }

    // Añadir botón de importación
    const importButton = document.createElement('button');
    importButton.textContent = 'Importar Eventos';
    importButton.className = 'btn btn-secondary mt-3 me-2';
    importButton.addEventListener('click', () => {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.json';
        input.onchange = e => {
            const file = e.target.files[0];
            importEvents(file);
        };
        input.click();
    });
    clearCalendarButton.parentNode.insertBefore(importButton, clearCalendarButton);
});       